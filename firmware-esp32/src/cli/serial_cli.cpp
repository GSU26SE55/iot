// ==================================================================
// Sprint 2 — S2-FW-01: Serial CLI implementation
// ==================================================================
#include "cli/serial_cli.h"

#include "core/identity_change_policy.h"
#include "net/mqtt_client.h"

#include <Arduino.h>
#include <string.h>

#if __has_include("config.h")
  #include "config.h"
#else
  #include "config.example.h"
#endif

#include "bms/bms_source.h"
#include "bms/modbus_bms.h"
#include "config/device_identity.h"
#include "config/nvs_store.h"
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

void printHelp() {
  Serial.println("---- Serial CLI (Sprint 2 + Sprint 4) ----");
  Serial.println("  show              — in trạng thái identity + MQTT (Sprint 4)");
  Serial.println("  set apikey <K>    — đổi API key (lưu NVS, hot reload)");
  Serial.println("  set devcode <C>   — đổi device code");
  Serial.println("  clear             — erase NVS, fallback compile-time");
  Serial.println("  reboot            — ESP.restart()");
  Serial.println("  help              — in help này");
  Serial.println("------------------------------------------");
}

const char* skipSpace(const char* p) {
  while (*p && (*p == ' ' || *p == '\t')) p++;
  return p;
}

bool startsWith(const char* line, const char* prefix) {
  return strncmp(line, prefix, strlen(prefix)) == 0;
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
    const auto decision = core::decideDeviceCodeChange(val, MQTT_USERNAME, kMqttEnabled);
    if (decision == core::IdentityChangeDecision::RejectInvalid) {
      Serial.println("[cli] TỪ CHỐI: deviceCode rỗng hoặc quá dài (tối đa 64 ký tự)");
      return;
    }
    if (decision == core::IdentityChangeDecision::RejectAclMismatch) {
      Serial.printf("[cli] TỪ CHỐI: '%s' không khớp MQTT_USERNAME='%s'.\n", val, MQTT_USERNAME);
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
