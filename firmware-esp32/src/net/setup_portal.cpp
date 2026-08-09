// ==================================================================
// IOT3-52/53 — hiện thực trang cấu hình. Xem setup_portal.h.
// ==================================================================
#include "net/setup_portal.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <string.h>

#include "config/device_identity.h"
#include "config/wifi_config.h"
#include "core/identity_validation.h"
#include "core/net_config_rules.h"
#include "net/wifi_scan_hints.h"

#if __has_include("config.h")
  #include "config.h"
#else
  #include "config.example.h"
#endif

namespace net {

namespace {

WiFiManager s_wm;
PortalMode  s_mode      = PortalMode::Off;
bool        s_active    = false;
uint32_t    s_startedMs = 0;

char s_apSsid[24];

/// Tham số thêm trên form — WiFiManager tự lo phần SSID/mật khẩu.
WiFiManagerParameter s_paramDeviceCode("devcode", "Mã thiết bị (DeviceCode)", "", 64);
WiFiManagerParameter s_paramApiKey    ("apikey",  "API key",                  "", 96);

/// 10 phút — xem lý do ở đầu header.
constexpr uint32_t kRecoveryTimeoutSec = 10UL * 60UL;

void buildApSsid() {
  if (s_apSsid[0] != '\0') return;
  const String mac = WiFi.macAddress();          // "AA:BB:CC:DD:EE:FF"
  char tail[5] = {'0', '0', '0', '0', '\0'};
  const int n = mac.length();
  if (n >= 5) {
    tail[0] = mac[n - 5]; tail[1] = mac[n - 4];  // bỏ dấu ':' ở giữa
    tail[2] = mac[n - 2]; tail[3] = mac[n - 1];
  }
  snprintf(s_apSsid, sizeof(s_apSsid), "SolarGW-%s", tail);
}

/// Khối HTML dán vào mọi trang: hai điều khách hay hiểu nhầm nhất.
const char* kHeadNotice =
    "<style>.solarnote{background:#fff8e1;border-left:4px solid #ffb300;"
    "padding:8px 10px;margin:8px 0;font-size:13px;line-height:1.5}</style>";

String buildScanNoticeHtml() {
  String html = "<div class='solarnote'><b>Trước khi chọn mạng</b><br>";
  html += net::wifi24GhzOnlyNotice();
  html += "<br>Sóng yếu hơn ";
  html += core::kWeakRssiDbm;
  html += " dBm sẽ nối được lúc thử nhưng rớt lai rai về sau — nên kê thiết bị gần router hơn."
          "<br>Mạng của công ty dùng tài khoản riêng cho từng người (WPA2-Enterprise) thì thiết bị "
          "<b>không nối được</b>; hãy dùng mạng thường hoặc phát 4G từ điện thoại.</div>";
  return html;
}

/// Danh sách mạng kèm cảnh báo — WiFiManager không cho chèn cảnh báo vào từng dòng của nó,
/// nên ta dựng bảng riêng đặt ngay dưới menu.
String buildScanTableHtml() {
  const int n = WiFi.scanNetworks();
  String html = "<div class='solarnote'><b>Mạng thiết bị dò được</b><br>";
  if (n <= 0) {
    html += "Không thấy mạng nào. ";
    html += net::wifi24GhzOnlyNotice();
  } else {
    html += "<table style='width:100%;font-size:12px'>";
    for (int i = 0; i < n && i < 15; ++i) {
      const int32_t rssi = WiFi.RSSI(i);
      const wifi_auth_mode_t auth = WiFi.encryptionType(i);
      html += "<tr><td>";
      html += WiFi.SSID(i);
      html += "</td><td>";
      html += rssi;
      html += " dBm</td><td>";
      html += net::describeAuthMode(auth);
      html += "</td><td>";
      if (net::isEnterpriseAuth(auth))     html += "❌ không nối được";
      else if (core::wifiSignalIsWeak(rssi)) html += "⚠ sóng yếu";
      else                                  html += "✅";
      html += "</td></tr>";
    }
    html += "</table>";
  }
  html += "</div>";
  WiFi.scanDelete();
  return html;
}

/// Chạy sau khi người dùng bấm Save. WiFiManager đã tự lưu SSID/mật khẩu vào NVS CỦA NÓ;
/// ta phải chép sang `wificfg` vì đó mới là nguồn chân lý của firmware này.
void onSaveConfig() {
  const String ssid = s_wm.getWiFiSSID(true);
  const String pass = s_wm.getWiFiPass(true);
  if (ssid.length() > 0) {
    if (!wificfg::save(ssid.c_str(), pass.c_str())) {
      Serial.println("[portal] ⚠ lưu WiFi vào NVS THẤT BẠI — kiểm tra log wificfg ở trên");
    }
  }

  const char* dev = s_paramDeviceCode.getValue();
  if (dev != nullptr && dev[0] != '\0') {
    if (identity::setDeviceCode(dev)) Serial.printf("[portal] đã đặt deviceCode=%s\n", dev);
    else                              Serial.println("[portal] ⚠ deviceCode KHÔNG hợp lệ — bỏ qua");
  }
  const char* key = s_paramApiKey.getValue();
  if (key != nullptr && key[0] != '\0') {
    if (identity::setApiKey(key)) Serial.println("[portal] đã đặt apiKey");
    else                          Serial.println("[portal] ⚠ apiKey KHÔNG hợp lệ — bỏ qua");
  }
}

}  // namespace

const char* portalApSsid() {
  buildApSsid();
  return s_apSsid;
}

bool portalStart(PortalMode mode) {
  if (mode == PortalMode::Off) return false;
  if (s_active) return false;

  // WPA2 đòi tối thiểu 8 ký tự. AP mở toang thì bất kỳ ai đi ngang cũng đổi được mạng và
  // API key của thiết bị — mà API key là thứ mở cửa toàn bộ đường HTTPS lên backend.
  const size_t pwLen = strlen(SETUP_AP_PASSWORD);
  if (pwLen < 8) {
    Serial.printf("[portal] TỪ CHỐI mở AP — SETUP_AP_PASSWORD chỉ %u ký tự, WPA2 cần ≥ 8.\n",
                  static_cast<unsigned>(pwLen));
    Serial.println("[portal]   Sửa trong include/config.h rồi nạp lại firmware.");
    return false;
  }

  buildApSsid();

  s_wm.setConfigPortalBlocking(false);   // ⚠ điều kiện tiên quyết — xem đầu header
  s_wm.setBreakAfterConfig(true);        // lưu xong là thoát, không bắt bấm thêm

  // Đặt mã thiết bị + API key CÙNG TRANG với WiFi (`_paramsInWifi = true`).
  // Tách sang trang riêng thì người lắp đặt điền WiFi, bấm Lưu, thiết bị thoát trang luôn —
  // và hai ô kia không bao giờ được điền. Mặc định của thư viện đã là chung trang; gọi tường
  // minh để một lần nâng cấp thư viện không lặng lẽ đổi hành vi này.
  s_wm.setParamsPage(false);
  s_wm.setTitle("Solar Gateway");
  s_wm.setCustomHeadElement(kHeadNotice);
  s_wm.setSaveConfigCallback(onSaveConfig);
  s_wm.addParameter(&s_paramDeviceCode);
  s_wm.addParameter(&s_paramApiKey);

  // Giá trị đang có, để người dùng thấy mình đang sửa cái gì.
  s_paramDeviceCode.setValue(identity::deviceCode(), 64);

  static String s_menuHtml;
  s_menuHtml = buildScanNoticeHtml() + buildScanTableHtml();
  s_wm.setCustomMenuHTML(s_menuHtml.c_str());

  const uint32_t timeoutSec = (mode == PortalMode::Recovery) ? kRecoveryTimeoutSec : 0;
  s_wm.setConfigPortalTimeout(timeoutSec);

  if (!s_wm.startConfigPortal(s_apSsid, SETUP_AP_PASSWORD)) {
    // Ở chế độ non-blocking, `startConfigPortal` trả false nghĩa là "chưa xong", KHÔNG phải lỗi.
  }

  s_active    = true;
  s_mode      = mode;
  s_startedMs = millis();

  Serial.println("========================================");
  Serial.printf(" TRANG CẤU HÌNH ĐANG MỞ — %s\n",
                mode == PortalMode::Recovery ? "chế độ PHỤC HỒI (10 phút)"
                                             : "lần cài đặt ĐẦU TIÊN (không giới hạn giờ)");
  Serial.printf("   1. Nối điện thoại vào WiFi:  %s\n", s_apSsid);
  Serial.printf("   2. Mật khẩu:                 %s\n", SETUP_AP_PASSWORD);
  Serial.println("   3. Trang cấu hình tự mở; nếu không, vào http://192.168.4.1");
  Serial.println("========================================");
  return true;
}

void portalTick() {
  if (!s_active) return;
  s_wm.process();

  // Hết hạn ở chế độ phục hồi: WiFiManager tự dừng, ta phải đồng bộ lại cờ để máy trạng thái
  // WiFi biết mà quay về chỉ-station.
  if (s_mode == PortalMode::Recovery &&
      (millis() - s_startedMs) >= kRecoveryTimeoutSec * 1000UL) {
    Serial.println("[portal] hết hạn 10 phút — đóng trang, tiếp tục thử mạng cũ");
    portalStop();
  }
}

void portalStop() {
  if (!s_active) return;
  s_wm.stopConfigPortal();
  s_active = false;
  s_mode   = PortalMode::Off;
  WiFi.mode(WIFI_STA);
  Serial.println("[portal] đã đóng trang cấu hình");
}

bool       portalIsActive()    { return s_active; }
PortalMode portalMode()        { return s_mode; }
uint32_t   portalStartedAtMs() { return s_startedMs; }

}  // namespace net
