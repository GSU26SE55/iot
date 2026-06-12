// ==================================================================
// Sprint 0 — S0-FW-03: WiFi connect + NTP sync
// Build: pio run
// Flash: pio run -t upload
// Mon:   pio device monitor -b 115200
//
// Acceptance (tasksprint.md S0-FW-03):
//   - Serial in chuỗi RFC3339 UTC mỗi giây, ví dụ "2026-06-12T08:15:42Z"
//   - Lệch giờ thực ≤ 1s (so với watch hoặc time.is)
//   - Rút WiFi → log "reconnecting"; bật lại → tự reconnect ≤ 30s
//
// Tham chiếu:
//   - newiot.md §12 #1 (bẫy clock skew → NTP retry)
//   - tasksprint.md S1-FW-02 (auto reconnect — phần này S1 sẽ tách module)
// ==================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

// include/config.h là file cá nhân (không commit).
// Nếu chưa tạo, dev copy từ config.example.h và điền WiFi.
#if __has_include("config.h")
  #include "config.h"
#else
  #include "config.example.h"
  #warning "config.h not found — fallback config.example.h (sửa WIFI_SSID/PASSWORD trong include/config.h)"
#endif

// ---- Trạng thái runtime ----
static bool        s_timeSynced   = false;
static uint32_t    s_lastPrintMs  = 0;
static uint32_t    s_lastReconnectMs = 0;
static uint32_t    s_lastNtpRetryMs  = 0;

// ---- Helper: in chuỗi ISO8601 UTC ----
static void printIsoNow() {
  struct tm tmUtc;
  if (!getLocalTime(&tmUtc, 100 /*ms*/)) {
    Serial.println("[time] not synced yet");
    return;
  }
  char buf[32];
  // 2026-06-12T08:15:42Z (RFC3339 UTC — backend yêu cầu UTC, không gửi +07)
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmUtc);
  Serial.printf("[%lus] isoNow=%s rssi=%ddBm wifi=%s\n",
                (unsigned long)(millis() / 1000),
                buf,
                WiFi.RSSI(),
                WiFi.status() == WL_CONNECTED ? "OK" : "DOWN");
}

// ---- WiFi ----
static void wifiBegin() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.printf("[wifi] connecting to \"%s\" ", WIFI_SSID);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 30000) {
    delay(500);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[wifi] connected ip=%s rssi=%ddBm\n",
                  WiFi.localIP().toString().c_str(),
                  WiFi.RSSI());
  } else {
    Serial.println("[wifi] FAILED (will retry in loop)");
  }
}

static void wifiEnsureConnected() {
  if (WiFi.status() == WL_CONNECTED) return;
  uint32_t now = millis();
  if (now - s_lastReconnectMs < 5000) return;   // tránh spam reconnect
  s_lastReconnectMs = now;
  Serial.println("[wifi] reconnecting...");
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

// ---- NTP ----
static void ntpBegin() {
  // GMT_OFFSET=0 → giờ trả về qua getLocalTime() chính là UTC.
  configTime(NTP_GMT_OFFSET_SEC, NTP_DAYLIGHT_OFFSET_SEC,
             NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
  Serial.println("[ntp] sync requested");
}

static bool ntpIsSynced() {
  // sntp_get_sync_status() không expose qua Arduino — check qua getLocalTime
  struct tm tm;
  if (!getLocalTime(&tm, 100)) return false;
  // Trước khi sync, ESP32 đặt epoch = 1970 → year < 2024 coi như chưa sync
  return (tm.tm_year + 1900) >= 2024;
}

static void ntpEnsureSynced() {
  if (s_timeSynced) return;
  if (WiFi.status() != WL_CONNECTED) return;

  uint32_t now = millis();
  if (now - s_lastNtpRetryMs < 5000) return;
  s_lastNtpRetryMs = now;

  if (ntpIsSynced()) {
    s_timeSynced = true;
    struct tm tm;
    getLocalTime(&tm);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    Serial.printf("[ntp] synced first time = %s (utc)\n", buf);
  } else {
    Serial.println("[ntp] not synced yet, retrying...");
    ntpBegin();   // request lại
  }
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 2000) { delay(10); }

  Serial.println();
  Serial.println("==============================");
  Serial.println(" Sprint 0 — S0-FW-03");
  Serial.println(" WiFi + NTP sync");
#ifdef FW_VERSION
  Serial.printf(" Version: %s\n", FW_VERSION);
#endif
#ifdef FW_BUILD_ENV
  Serial.printf(" Env:     %s\n", FW_BUILD_ENV);
#endif
  Serial.println("==============================");

  wifiBegin();
  ntpBegin();
}

void loop() {
  wifiEnsureConnected();
  ntpEnsureSynced();

  uint32_t now = millis();
  if (now - s_lastPrintMs >= 1000) {
    s_lastPrintMs = now;
    if (s_timeSynced) {
      printIsoNow();
    } else {
      Serial.printf("[%lus] waiting for ntp sync (wifi=%s)\n",
                    (unsigned long)(now / 1000),
                    WiFi.status() == WL_CONNECTED ? "OK" : "DOWN");
    }
  }

  delay(10);
}
