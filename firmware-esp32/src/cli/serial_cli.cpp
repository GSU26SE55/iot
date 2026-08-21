// ==================================================================
// Sprint 2 — S2-FW-01: Serial CLI implementation
// ==================================================================
#include "cli/serial_cli.h"

#include "core/identity_change_policy.h"
#include "net/mqtt_client.h"
#include "provision/provision.h"

#include <Arduino.h>
#include <WiFi.h>
#include <string.h>

#if __has_include("config.h")
  #include "config.h"
#else
  #include "config.example.h"
#endif

#include "bms/bms_source.h"
#include "bms/modbus_bms.h"
#include "config/battery_map_runtime.h"
#include "config/device_identity.h"
#include "config/mqtt_config.h"
#include "config/nvs_store.h"
#include "config/wifi_config.h"
#include "net/wifi_manager.h"
#include "net/wifi_scan_hints.h"
#include "net/mqtt_client.h"
#include "sensor/ds18b20.h"
#include "sensor/ina226.h"
#include "sensor/sht31.h"

namespace cli {

namespace {
// GH-747 — firmware này LUÔN dùng MQTT (config chỉ có MQTT_USE_TLS để chọn plain/TLS, không
// có cờ bật/tắt). Giữ hằng số tường minh thay vì hằng `true` trần: nếu sau này thêm
// MQTT_ENABLED thì đây là đúng một chỗ phải sửa, và policy đã nhận sẵn tham số này.
constexpr bool kMqttEnabled = true;
}  // namespace


namespace {
constexpr size_t kMaxLineLen = 128;

char     s_buf[kMaxLineLen];
size_t   s_idx = 0;

const char* skipSpace(const char* p) {
  while (*p && (*p == ' ' || *p == '\t')) p++;
  return p;
}

bool startsWith(const char* line, const char* prefix) {
  return strncmp(line, prefix, strlen(prefix)) == 0;
}

void printHelp() {
  Serial.println("---- Serial CLI (Sprint 2 + 4 + IoT-3) ----");
  Serial.println("  show                    — trạng thái identity + WiFi + MQTT + bảng pin");
  Serial.println("  set apikey <K>          — đổi API key (lưu NVS, hot reload)");
  Serial.println("  set devcode <C>         — đổi device code");
  Serial.println("  -- mạng (IOT3-55) --");
  Serial.println("  set wifi <ssid> <pass>  — đổi WiFi + nối lại NGAY (không reboot)");
  Serial.println("  set wifissid <ssid>     — chỉ đổi SSID (dùng khi SSID CÓ dấu cách)");
  Serial.println("  set wifipass <pass>     — chỉ đổi mật khẩu (dùng khi mật khẩu CÓ dấu cách)");
  Serial.println("  wifiscan                — quét mạng quanh đây + cảnh báo sóng yếu/Enterprise");
  Serial.println("  -- broker (IOT3-55) --");
  Serial.println("  set mqttbroker <host> <port>");
  Serial.println("  set mqttuser <U>        — username MQTT");
  Serial.println("  set mqttpass <P>        — mật khẩu MQTT");
  Serial.println("  set mqttprefix <T>      — tiền tố topic, vd solar/gw-esp32-001");
  Serial.println("  -- khác --");
  Serial.println("  clear                   — erase NVS, fallback compile-time");
  Serial.println("  resume                  — phục hồi cờ provision, giữ nguyên mọi cấu hình");
  Serial.println("  reboot                  — ESP.restart()");
  Serial.println("  help                    — in help này");
  Serial.println("------------------------------------------");
  Serial.println("  Thường ngày KHÔNG cần các lệnh `set mqtt*`: /provision tự cấp.");
  Serial.println("  Chúng là đường tay khi backend chưa sẵn sàng hoặc đang truy sự cố.");
}

/// <summary>Tách `"<a> <b>"` tại dấu cách ĐẦU TIÊN.</summary>
/// <returns>Con trỏ tới phần sau, hoặc nullptr nếu không có dấu cách.</returns>
const char* splitFirstSpace(const char* text, char* firstOut, size_t firstLen) {
  const char* sp = strchr(text, ' ');
  if (sp == nullptr) return nullptr;
  size_t n = static_cast<size_t>(sp - text);
  if (n >= firstLen) n = firstLen - 1;
  memcpy(firstOut, text, n);
  firstOut[n] = '\0';
  return skipSpace(sp);
}

void doWifiScan() {
  Serial.println("[cli] đang quét WiFi (~3 giây)...");
  const int n = WiFi.scanNetworks();
  if (n <= 0) {
    Serial.println("[cli] không thấy mạng nào.");
    Serial.println("[cli]   ESP32-S3 CHỈ bắt được 2,4 GHz. Router chỉ phát 5 GHz thì sẽ");
    Serial.println("[cli]   không bao giờ hiện ở đây, dù điện thoại vẫn vào bình thường.");
    WiFi.scanDelete();
    return;
  }
  Serial.printf("[cli] thấy %d mạng (chỉ 2,4 GHz — phần cứng không bắt 5 GHz):\n", n);
  for (int i = 0; i < n; ++i) {
    const int32_t rssi = WiFi.RSSI(i);
    const wifi_auth_mode_t auth = WiFi.encryptionType(i);
    Serial.printf("  %2d) %-32s %4lddBm  %s", i + 1, WiFi.SSID(i).c_str(),
                  static_cast<long>(rssi), net::describeAuthMode(auth));
    if (core::wifiSignalIsWeak(rssi)) {
      Serial.printf("   ⚠ SÓNG YẾU (< %d dBm) — hay rớt, nên kê lại vị trí", core::kWeakRssiDbm);
    }
    if (net::isEnterpriseAuth(auth)) {
      Serial.print("   ⚠ WPA2-ENTERPRISE — firmware KHÔNG nối được loại này");
    }
    Serial.println();
  }
  Serial.printf("[cli] %s\n", net::wifi24GhzOnlyNotice());
  WiFi.scanDelete();
}

void executeCommand(const char* line) {
  if (line[0] == '\0') return;

  Serial.printf("[cli] > %s\n", line);

  if (startsWith(line, "help") || strcmp(line, "?") == 0) {
    printHelp();
    return;
  }
  if (strcmp(line, "show") == 0) {
    identity::printStatus();
    Serial.printf("  uptime     = %lus\n", static_cast<unsigned long>(millis() / 1000));
    Serial.printf("  free heap  = %u bytes\n", static_cast<unsigned>(ESP.getFreeHeap()));
    Serial.printf("  NVS free   = %u entries\n",
                  static_cast<unsigned>(storage::nvsFreeEntries()));
    // IOT3-55 — nguồn cấu hình mạng. Câu hỏi đầu tiên khi truy sự cố luôn là
    // "nó đang đọc NVS hay đang đọc bản build?".
    wificfg::printStatus();
    mqttcfg::printStatus();
    batmap::printStatus();
    // Sprint 4 — MQTT broker status.
    Serial.println("  -- MQTT (Sprint 4) --");
    Serial.printf("  connected  = %s\n", net::mqttIsConnected() ? "YES" : "NO");
    Serial.printf("  reconnects = %lu\n",
                  static_cast<unsigned long>(net::mqttConnectCount()));
    Serial.printf("  pub ok     = %lu\n",
                  static_cast<unsigned long>(net::mqttPublishOkCount()));
    Serial.printf("  pub fail   = %lu\n",
                  static_cast<unsigned long>(net::mqttPublishFailCount()));
    Serial.printf("  fail streak= %lu (threshold %d → fallback HTTPS)\n",
                  static_cast<unsigned long>(net::mqttConsecutiveFailCount()),
                  static_cast<int>(MQTT_PUBLISH_FAIL_THRESHOLD));
    // Sprint 5 — BMS source + sensor status.
    Serial.println("  -- BMS + Sensors (Sprint 5) --");
    Serial.printf("  mode       = %s\n", bms::bmsSourceMode());
#if !USE_MOCK_BMS
    Serial.printf("  modbus     = ok=%lu fail=%lu\n",
                  static_cast<unsigned long>(bms::modbusPollOkCount()),
                  static_cast<unsigned long>(bms::modbusPollFailCount()));
    Serial.printf("  ina226     = ok=%lu fail=%lu (redundant cross-source)\n",
                  static_cast<unsigned long>(sensor::ina226ReadOkCount()),
                  static_cast<unsigned long>(sensor::ina226ReadFailCount()));
    Serial.printf("  ds18b20    = ok=%lu fail=%lu (external-temp)\n",
                  static_cast<unsigned long>(sensor::ds18b20ReadOkCount()),
                  static_cast<unsigned long>(sensor::ds18b20ReadFailCount()));
    Serial.printf("  sht31      = post ok=%lu fail=%lu (ambient)\n",
                  static_cast<unsigned long>(sensor::sht31PostOkCount()),
                  static_cast<unsigned long>(sensor::sht31PostFailCount()));
#else
    Serial.println("  (mock mode — sensor counters n/a)");
#endif
    return;
  }
  if (strcmp(line, "clear") == 0) {
    if (identity::resetToDefaults()) {
      Serial.println("[cli] NVS cleared. Reboot để áp dụng fully.");
    } else {
      Serial.println("[cli] NVS erase FAILED");
    }
    return;
  }
  if (strcmp(line, "resume") == 0) {
    if (identity::deviceCode()[0] == '\0' || identity::apiKey()[0] == '\0' ||
        mqttcfg::host()[0] == '\0' ||
        mqttcfg::username()[0] == '\0' || mqttcfg::password()[0] == '\0') {
      Serial.println("[cli] KHÔNG resume: thiếu identity hoặc cấu hình MQTT đã lưu");
      return;
    }
    if (!provision::restoreProvisionFlag()) {
      Serial.println("[cli] resume FAILED: không ghi được cờ provision");
      return;
    }
    Serial.println("[cli] provision flag restored; rebooting in 1s...");
    Serial.flush();
    delay(1000);
    ESP.restart();
    return;
  }
  if (strcmp(line, "reboot") == 0) {
    Serial.println("[cli] rebooting in 1s...");
    Serial.flush();           // ensure log out trước reboot (USB-CDC buffered)
    delay(1000);
    ESP.restart();
    return;
  }
  if (startsWith(line, "set apikey ")) {
    const char* val = skipSpace(line + strlen("set apikey "));
    if (val[0] == '\0') {
      Serial.println("[cli] thiếu giá trị. Usage: set apikey <key>");
      return;
    }
    if (identity::setApiKey(val)) {
      Serial.println("[cli] apiKey saved → hot reloaded (không cần reboot)");
    } else {
      Serial.println("[cli] setApiKey FAILED");
    }
    return;
  }
  if (startsWith(line, "set devcode ")) {
    const char* val = skipSpace(line + strlen("set devcode "));
    if (val[0] == '\0') {
      Serial.println("[cli] thiếu giá trị. Usage: set devcode <code>");
      return;
    }
    // GH-747 — kiểm TRƯỚC khi ghi. Đổi sang một deviceCode không khớp username MQTT sẽ
    // làm thiết bị câm cả hai chiều (publish bị ACL từ chối, downlink về topic cũ) mà log
    // vẫn báo "hot reloaded". Thà từ chối còn hơn để thiết bị chết câm ngoài hiện trường.
    // IOT3-56 — đối chiếu với username MQTT ĐANG DÙNG (`mqttcfg`), không phải macro compile-time.
    // Từ sprint này username do `/provision` cấp lúc chạy; so với macro sẽ vừa từ chối oan
    // (username thật đã đổi) vừa chấp nhận nhầm (macro trùng nhưng NVS thì không).
    const auto decision = core::decideDeviceCodeChange(val, mqttcfg::username(), kMqttEnabled);
    if (decision == core::IdentityChangeDecision::RejectInvalid) {
      Serial.println("[cli] TỪ CHỐI: deviceCode rỗng hoặc quá dài (tối đa 64 ký tự)");
      return;
    }
    if (decision == core::IdentityChangeDecision::RejectAclMismatch) {
      Serial.printf("[cli] TỪ CHỐI: '%s' không khớp username MQTT đang dùng ('%s').\n",
                    val, mqttcfg::username());
      Serial.println("[cli]   Backend đặt mqtt_username = lowercase(deviceCode), nên đổi code");
      Serial.println("[cli]   mà không đổi credential là mất cả uplink lẫn downlink.");
      Serial.println("[cli]   → provision lại thiết bị (lấy MqttUsername/MqttPassword mới) rồi flash.");
      return;
    }

    if (identity::setDeviceCode(val)) {
      Serial.println("[cli] deviceCode saved → hot reloaded");
      // Ngắt phiên MQTT cũ để tick kế dựng lại LWT + topic + subscribe theo code mới.
      net::mqttOnIdentityChanged();
    } else {
      Serial.println("[cli] setDeviceCode FAILED");
    }
    return;
  }

  // ---------------- IOT3-55: mạng + broker ----------------
  //
  // ⚠️ Thứ tự kiểm QUAN TRỌNG: "set wifissid ..." cũng bắt đầu bằng "set wifi", nên hai lệnh
  // riêng lẻ phải đứng TRƯỚC dạng gộp "set wifi <ssid> <pass>" (có dấu cách sau "wifi").

  if (startsWith(line, "set wifissid ")) {
    const char* val = skipSpace(line + strlen("set wifissid "));
    if (val[0] == '\0') { Serial.println("[cli] Usage: set wifissid <ssid>"); return; }
    // Sao mật khẩu ra biến cục bộ: `wificfg::save()` sẽ ghi đè đúng buffer mà
    // `wificfg::password()` đang trỏ tới.
    char pass[wificfg::kPassBufLen];
    snprintf(pass, sizeof(pass), "%s", wificfg::password());
    if (net::wifiReconfigure(val, pass)) Serial.println("[cli] đã đổi SSID + nối lại");
    return;
  }
  if (startsWith(line, "set wifipass ")) {
    const char* val = skipSpace(line + strlen("set wifipass "));
    char ssid[wificfg::kSsidBufLen];
    snprintf(ssid, sizeof(ssid), "%s", wificfg::ssid());
    if (ssid[0] == '\0') {
      Serial.println("[cli] chưa có SSID — đặt `set wifissid <ssid>` trước.");
      return;
    }
    if (net::wifiReconfigure(ssid, val)) Serial.println("[cli] đã đổi mật khẩu + nối lại");
    return;
  }
  if (startsWith(line, "set wifi ")) {
    const char* rest = skipSpace(line + strlen("set wifi "));
    char ssid[wificfg::kSsidBufLen];
    const char* pass = splitFirstSpace(rest, ssid, sizeof(ssid));
    if (pass == nullptr) {
      Serial.println("[cli] Usage: set wifi <ssid> <mật khẩu>");
      Serial.println("[cli]   Mạng mở (không mật khẩu): `set wifissid <ssid>` rồi `set wifipass `");
      return;
    }
    // Dạng gộp tách tại dấu cách ĐẦU TIÊN, nên SSID có dấu cách sẽ bị hiểu sai. Nói thẳng ra
    // thay vì để người dùng loay hoay vì sao nối vào một mạng không tồn tại.
    if (strchr(pass, ' ') != nullptr) {
      Serial.println("[cli] ⚠ phần sau còn dấu cách — nếu SSID CỦA BẠN có dấu cách, hãy dùng");
      Serial.println("[cli]   `set wifissid <ssid>` và `set wifipass <mật khẩu>` riêng.");
    }
    if (net::wifiReconfigure(ssid, pass)) Serial.println("[cli] đã đổi WiFi + nối lại");
    return;
  }
  if (strcmp(line, "wifiscan") == 0) {
    doWifiScan();
    return;
  }
  if (startsWith(line, "set mqttbroker ")) {
    const char* rest = skipSpace(line + strlen("set mqttbroker "));
    char host[mqttcfg::kHostBufLen];
    const char* portText = splitFirstSpace(rest, host, sizeof(host));
    if (portText == nullptr) {
      Serial.println("[cli] Usage: set mqttbroker <host> <port>");
      return;
    }
    if (mqttcfg::setBroker(host, atoi(portText))) net::mqttApplyConfig();
    return;
  }
  if (startsWith(line, "set mqttuser ")) {
    const char* val = skipSpace(line + strlen("set mqttuser "));
    char pass[mqttcfg::kPassBufLen];
    snprintf(pass, sizeof(pass), "%s", mqttcfg::password());
    if (mqttcfg::setCredential(val, pass)) net::mqttApplyConfig();
    return;
  }
  if (startsWith(line, "set mqttpass ")) {
    const char* val = skipSpace(line + strlen("set mqttpass "));
    char user[mqttcfg::kUserBufLen];
    snprintf(user, sizeof(user), "%s", mqttcfg::username());
    if (mqttcfg::setCredential(user, val)) net::mqttApplyConfig();
    return;
  }
  if (startsWith(line, "set mqttprefix ")) {
    const char* val = skipSpace(line + strlen("set mqttprefix "));
    if (mqttcfg::setTopicPrefix(val)) net::mqttApplyConfig();
    return;
  }

  Serial.printf("[cli] unknown command: \"%s\" — gõ `help` để xem list.\n", line);
}
}  // namespace

void cliBegin() {
  s_idx = 0;
  s_buf[0] = '\0';
  Serial.println("[cli] Serial CLI ready — gõ `help` để xem commands.");
}

void cliTick() {
  while (Serial.available() > 0) {
    int c = Serial.read();
    if (c < 0) break;
    if (c == '\r') continue;       // ignore CR (Windows CRLF)
    if (c == '\n') {
      s_buf[s_idx] = '\0';
      executeCommand(s_buf);
      s_idx = 0;
      s_buf[0] = '\0';
      continue;
    }
    if (s_idx < kMaxLineLen - 1) {
      s_buf[s_idx++] = static_cast<char>(c);
    } else {
      // Overflow — flush + warning
      s_buf[kMaxLineLen - 1] = '\0';
      Serial.println("[cli] line too long, dropped");
      s_idx = 0;
      s_buf[0] = '\0';
    }
  }
}

}  // namespace cli
