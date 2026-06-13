// ==================================================================
// Sprint 1 — S1-FW-02: WiFi manager implementation
//
// Acceptance (tasksprint.md S1-FW-02):
//   - Tắt WiFi router → ESP32 log "reconnecting"
//   - Bật lại → reconnect ≤ 30s
//
// Thiết kế:
//   - Dùng WiFi.setAutoReconnect(true) (ESP32 SDK tự thử lại trong driver)
//   - + thêm tầng watchdog ở tick(): nếu sau 5s vẫn DOWN → gọi reconnect()
//     để cover trường hợp driver bỏ cuộc (ví dụ DHCP fail).
//   - Throttle 5s tránh spam reconnect (làm rớt WiFi tệ hơn).
// ==================================================================
#include "net/wifi_manager.h"

#include <Arduino.h>
#include <WiFi.h>

namespace net {

namespace {
constexpr uint32_t kReconnectThrottleMs = 5000;

const char*  s_ssid     = nullptr;
const char*  s_password = nullptr;
uint32_t     s_lastReconnectMs = 0;
bool         s_wasConnected    = false;   // edge detect connect/disconnect

void onWiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.printf("[wifi] GOT_IP  ip=%s rssi=%ddBm\n",
                    WiFi.localIP().toString().c_str(),
                    WiFi.RSSI());
      s_wasConnected = true;
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      // ESP32 sẽ tự reconnect (autoReconnect=true), log ra để dev biết.
      if (s_wasConnected) {
        Serial.println("[wifi] DISCONNECTED (auto-reconnect armed)");
      }
      s_wasConnected = false;
      break;
    default:
      break;
  }
}
}  // namespace

void wifiBegin(const char* ssid, const char* password) {
  s_ssid     = ssid;
  s_password = password;

  WiFi.onEvent(onWiFiEvent);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);          // tránh ghi creds vào NVS (Sprint 2 sẽ quản lý riêng)
  WiFi.setSleep(false);            // tăng độ ổn định MQTT/HTTPS (tiêu thụ thêm ~30mA)

  WiFi.begin(ssid, password);

  Serial.printf("[wifi] connecting to \"%s\" ", ssid);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 30000) {
    delay(500);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[wifi] connected ip=%s rssi=%ddBm mac=%s\n",
                  WiFi.localIP().toString().c_str(),
                  WiFi.RSSI(),
                  WiFi.macAddress().c_str());
  } else {
    Serial.println("[wifi] initial connect FAILED — will retry in loop");
  }
}

void wifiTick() {
  if (WiFi.status() == WL_CONNECTED) return;

  uint32_t now = millis();
  if (now - s_lastReconnectMs < kReconnectThrottleMs) return;
  s_lastReconnectMs = now;

  Serial.println("[wifi] reconnecting...");
  WiFi.disconnect();
  WiFi.begin(s_ssid, s_password);
}

bool wifiIsConnected() {
  return WiFi.status() == WL_CONNECTED;
}

int8_t wifiRssi() {
  return WiFi.status() == WL_CONNECTED ? static_cast<int8_t>(WiFi.RSSI()) : 0;
}

}  // namespace net
