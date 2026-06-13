// ==================================================================
// Sprint 2 — S2-FW-03: Heartbeat implementation
//
// Field mapping ESP32 → backend IotDeviceHeartbeatCommand (per MO §52.2 + §52.4):
//
//   ESP32 metric              JSON field             Backend property
//   ─────────────────────────────────────────────────────────────────
//   temperatureRead()         Temperature            decimal? Temperature
//   ESP.getFreeHeap()/MB      MemoryUsageMb          long? MemoryUsageMb
//   WiFi.RSSI()               SignalStrengthDbm      int? SignalStrengthDbm (alias RssiDbm)
//   queue.size() (=0 S2)      LocalQueueDepth        int? LocalQueueDepth (alias QueuedReadingCount)
//   FW_VERSION                FirmwareVersion        string?
//   millis()/1000             UptimeSeconds          long?
//   isoNow()                  DeviceTimestamp        DateTime (required)
//   null                      Cpu                    decimal? — ESP32 không tính
//   null                      DiskFreeMb             long? — ESP32 không có disk
// ==================================================================
#include "telemetry/heartbeat.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>

#include "net/http_client.h"
#include "net/time_sync.h"

#if __has_include("config.h")
  #include "config.h"
#else
  #include "config.example.h"
#endif

// ESP32 internal temperature sensor: `temperatureRead()` available directly trong <Arduino.h>
// kể từ Arduino-ESP32 v2.0.5. ESP-IDF v5+ deprecates driver/temperature_sensor.h.

namespace telemetry {

namespace {
constexpr const char* kEndpoint = "/api/iot-devices/heartbeat";

uint32_t s_intervalMs    = 60000UL;     // default 60s
uint32_t s_lastSentMs    = 0;
bool     s_firstTick     = true;
uint32_t s_okCount       = 0;
uint32_t s_failCount     = 0;

// ESP-IDF v5 (Arduino-ESP32 v3.x) đã có temperature_sensor handle riêng.
// Sprint 2: cache giá trị đọc cuối, retry nếu fail.
float readChipTemperatureC() {
  // Arduino-ESP32 v3.x: temperatureRead() trả °C trực tiếp.
  // Một số board lệch ~+5°C so với ambient — chấp nhận cho monitoring.
  return temperatureRead();
}

uint32_t freeHeapMb() {
  // ESP.getFreeHeap() = internal SRAM heap (KB-range on ESP32 base).
  // ESP.getFreePsram() = PSRAM heap nếu enabled (MB-range on N16R8).
  // Cộng cả 2 + rounded up division để không luôn ra 0 trên board không PSRAM.
  uint32_t totalBytes = ESP.getFreeHeap() + ESP.getFreePsram();
  // Round to nearest MB (≥ 512KB → 1MB), tránh underreport.
  return (totalBytes + 512UL * 1024UL) / (1024UL * 1024UL);
}

float freeHeapPercent() {
  // % free internal heap — useful trên board N8 (no PSRAM) where MemoryUsageMb=0.
  uint32_t total = ESP.getHeapSize();
  if (total == 0) return 0.0f;
  return (static_cast<float>(ESP.getFreeHeap()) * 100.0f) / static_cast<float>(total);
}

int8_t wifiRssi() {
  return WiFi.status() == WL_CONNECTED ? static_cast<int8_t>(WiFi.RSSI()) : 0;
}

bool buildBody(char* outBuf, size_t outBufLen, size_t& outLen) {
  char ts[24];
  if (!net::isoNow(ts, sizeof(ts))) return false;

  JsonDocument doc;
  doc["DeviceTimestamp"]   = ts;
  doc["FirmwareVersion"]   = FW_VERSION;
  doc["Temperature"]       = readChipTemperatureC();          // decimal? — ESP32 chip temp
  doc["MemoryUsageMb"]     = freeHeapMb();                    // long?    — free heap+PSRAM MB
  doc["FreeMemoryPercent"] = freeHeapPercent();               // decimal? — useful nếu board không PSRAM
  doc["SignalStrengthDbm"] = wifiRssi();                      // int?     — WiFi RSSI
  doc["LocalQueueDepth"]   = 0;                               // int?     — Sprint 3 sẽ có queue thật
  doc["UptimeSeconds"]     = static_cast<long>(millis() / 1000);
  // Backend command nhận `Cpu` / `DiskFreeMb` optional decimal/long → null OK trên ESP32.
  doc["Cpu"]               = nullptr;
  doc["DiskFreeMb"]        = nullptr;

  outLen = serializeJson(doc, outBuf, outBufLen);
  return outLen > 0 && outLen < outBufLen;
}

bool sendOnce() {
  char body[512];
  size_t bodyLen = 0;
  if (!buildBody(body, sizeof(body), bodyLen)) {
    Serial.println("[heartbeat] FAIL: build body (NTP chưa sync?)");
    s_failCount++;
    return false;
  }

  net::PostResult res = net::httpPostJson(kEndpoint, body, bodyLen);
  bool ok = (res.httpCode >= 200 && res.httpCode < 300);
  if (ok) {
    s_okCount++;
    Serial.printf("[heartbeat] ok#%lu (%d, %lums, %u→%u bytes)\n",
                  static_cast<unsigned long>(s_okCount),
                  res.httpCode,
                  static_cast<unsigned long>(res.durationMs),
                  static_cast<unsigned>(bodyLen),
                  static_cast<unsigned>(res.responseBytes));
  } else {
    s_failCount++;
    Serial.printf("[heartbeat] FAIL#%lu code=%d body=\"%s\"\n",
                  static_cast<unsigned long>(s_failCount),
                  res.httpCode, res.responseSnippet);
  }
  return ok;
}
}  // namespace

void heartbeatBegin(uint32_t intervalMs) {
  // Apply bound check ngay khi init — defensive nếu NVS đọc về giá trị bất thường (0, MAX_UINT, ...).
  if (intervalMs < 10000)   intervalMs = 60000;     // 0 hoặc < 10s → fallback default 60s
  if (intervalMs > 3600000) intervalMs = 3600000;   // cap 1h
  s_intervalMs   = intervalMs;
  s_lastSentMs   = 0;
  s_firstTick    = true;
  s_okCount      = 0;
  s_failCount    = 0;
  Serial.printf("[heartbeat] init interval=%lums (target endpoint=%s)\n",
                static_cast<unsigned long>(intervalMs), kEndpoint);
}

void heartbeatSetInterval(uint32_t intervalMs) {
  if (intervalMs < 10000) intervalMs = 10000;      // min 10s — backend bound
  if (intervalMs > 3600000) intervalMs = 3600000;  // max 1h
  if (s_intervalMs != intervalMs) {
    Serial.printf("[heartbeat] interval %lums → %lums\n",
                  static_cast<unsigned long>(s_intervalMs),
                  static_cast<unsigned long>(intervalMs));
    s_intervalMs = intervalMs;
  }
}

void heartbeatTick() {
  uint32_t now = millis();
  // Gửi heartbeat đầu tiên 5s sau boot (chờ NTP sync + provision xong).
  if (s_firstTick) {
    if (now < 5000) return;
    s_firstTick = false;
    s_lastSentMs = now - s_intervalMs;   // force gửi ngay tick này
  }
  if (now - s_lastSentMs < s_intervalMs) return;
  s_lastSentMs = now;
  sendOnce();
}

bool heartbeatSendNow() {
  s_lastSentMs = millis();
  return sendOnce();
}

uint32_t heartbeatOkCount()   { return s_okCount;   }
uint32_t heartbeatFailCount() { return s_failCount; }

}  // namespace telemetry
