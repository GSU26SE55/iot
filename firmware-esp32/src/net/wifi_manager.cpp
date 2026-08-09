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

#include "config/wifi_config.h"
#include "net/setup_portal.h"

namespace net {

namespace {
constexpr uint32_t kReconnectThrottleMs = 5000;

// IOT3-50 — KHÔNG cache SSID/mật khẩu ở đây. `wificfg::` là nguồn chân lý duy nhất; cache lại
// là tự tạo nguồn thứ hai, và hai nguồn thì sớm muộn cũng lệch.
uint32_t     s_lastReconnectMs = 0;
bool         s_wasConnected    = false;   // edge detect connect/disconnect

// IOT3-51 — máy trạng thái.
WifiPhase s_phase        = WifiPhase::Unconfigured;
uint32_t  s_offlineSince = 0;      // millis lúc bắt đầu mất mạng; 0 = đang có mạng

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

void wifiBegin() {
  WiFi.onEvent(onWiFiEvent);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);          // tránh ghi creds vào NVS (Sprint 2 sẽ quản lý riêng)
  WiFi.setSleep(false);            // tăng độ ổn định MQTT/HTTPS (tiêu thụ thêm ~30mA)

  if (!wificfg::isConfigured()) {
    Serial.println("[wifi] CHƯA CẤU HÌNH mạng — bỏ qua bước nối.");
    Serial.println("[wifi]   → trang cấu hình sẽ tự mở ở tick đầu tiên của loop().");
    Serial.println("[wifi]   → hoặc dùng Serial CLI: `set wifi <ssid> <mật khẩu>`");
    s_phase = WifiPhase::Unconfigured;
    return;
  }
  s_phase = WifiPhase::Connecting;

  WiFi.begin(wificfg::ssid(), wificfg::password());

  Serial.printf("[wifi] connecting to \"%s\" ", wificfg::ssid());
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
    s_phase        = WifiPhase::Connected;
    s_offlineSince = 0;
  } else {
    Serial.println("[wifi] initial connect FAILED — will retry in loop");
    s_offlineSince = millis();
  }
}

void wifiTick() {
  const uint32_t now = millis();

  // Trang cấu hình phải được bơm TRƯỚC mọi quyết định khác — nó là thứ duy nhất cho người ta
  // đường thoát khi thiết bị không nối được mạng.
  if (portalIsActive()) portalTick();

  // IOT3-51 — QUYẾT ĐỊNH nằm ở `core::decideWifiPhase` (thuần, test ở env:native);
  // phần dưới đây chỉ THI HÀNH: bật/tắt AP, gọi WiFi.begin, in log.
  const bool connected = (WiFi.status() == WL_CONNECTED);
  const uint32_t offlineMsNow =
      (connected || s_offlineSince == 0) ? 0 : (now - s_offlineSince);
  const core::WifiPhaseDecision decision =
      core::decideWifiPhase(wificfg::isConfigured(), connected, offlineMsNow,
                            kRecoveryAfterOfflineMs);

  // ---------------- có mạng ----------------
  if (decision == core::WifiPhaseDecision::Connected) {
    if (s_phase != WifiPhase::Connected) {
      Serial.printf("[wifi] có mạng trở lại sau %lus\n",
                    static_cast<unsigned long>(wifiOfflineDurationMs() / 1000UL));
      s_phase = WifiPhase::Connected;
    }
    s_offlineSince = 0;
    // Nối được rồi thì đóng AP: để AP chạy tiếp vừa tốn sóng vừa khiến điện thoại
    // của khách bám vào AP của thiết bị thay vì mạng nhà.
    if (portalIsActive()) {
      Serial.println("[wifi] đã nối được mạng — đóng trang cấu hình");
      portalStop();
    }
    return;
  }

  // ---------------- chưa cấu hình ----------------
  if (decision == core::WifiPhaseDecision::Unconfigured) {
    if (s_phase != WifiPhase::Unconfigured) {
      Serial.println("[wifi] không có SSID trong NVS — chuyển sang chế độ cài đặt");
      s_phase = WifiPhase::Unconfigured;
    }
    if (!portalIsActive()) portalStart(PortalMode::FirstTimeSetup);
    return;
  }

  // ---------------- mất mạng ----------------
  if (s_offlineSince == 0) s_offlineSince = now;
  const uint32_t offlineMs = now - s_offlineSince;

  if (decision == core::WifiPhaseDecision::Recovery) {
    if (s_phase != WifiPhase::Recovery) {
      Serial.printf("[wifi] mất mạng %lu phút — bật chế độ PHỤC HỒI: phát AP cấu hình NHƯNG "
                    "vẫn tiếp tục thử mạng cũ\n",
                    static_cast<unsigned long>(offlineMs / 60000UL));
      s_phase = WifiPhase::Recovery;
    }
    // `WIFI_AP_STA` chứ không phải `WIFI_AP`: mất chế độ station là mất luôn khả năng tự lành
    // khi router khách sống lại, và sẽ phải cử người ra tận nơi vì một sự cố tự hết.
    if (!portalIsActive() && portalStart(PortalMode::Recovery)) {
      WiFi.mode(WIFI_AP_STA);
    }
  } else if (s_phase != WifiPhase::Connecting) {
    s_phase = WifiPhase::Connecting;
  }

  // Thử lại mạng cũ — chạy ở CẢ hai chế độ Connecting lẫn Recovery.
  if (now - s_lastReconnectMs < kReconnectThrottleMs) return;
  s_lastReconnectMs = now;

  Serial.println("[wifi] reconnecting...");
  WiFi.disconnect();
  WiFi.begin(wificfg::ssid(), wificfg::password());
}

WifiPhase wifiPhase() { return s_phase; }

uint32_t wifiOfflineDurationMs() {
  return s_offlineSince == 0 ? 0 : (millis() - s_offlineSince);
}

bool wifiReconfigure(const char* ssid, const char* password) {
  if (!wificfg::save(ssid, password)) return false;   // đã log lý do

  Serial.printf("[wifi] đổi sang mạng \"%s\" — nối lại ngay, không khởi động lại\n",
                wificfg::ssid());
  WiFi.disconnect();
  // Xoá throttle để wifiTick() không phải đợi thêm 5 giây nữa mới thử.
  s_lastReconnectMs = 0;
  WiFi.begin(wificfg::ssid(), wificfg::password());
  return true;
}

bool wifiIsConnected() {
  return WiFi.status() == WL_CONNECTED;
}

int8_t wifiRssi() {
  return WiFi.status() == WL_CONNECTED ? static_cast<int8_t>(WiFi.RSSI()) : 0;
}

}  // namespace net
