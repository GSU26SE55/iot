// ==================================================================
// Sprint 2 — S2-FW-01: Serial CLI implementation
// ==================================================================
#include "cli/serial_cli.h"

#include <Arduino.h>
#include <string.h>

#if __has_include("config.h")
  #include "config.h"
#else
  #include "config.example.h"
#endif

#include "config/device_identity.h"
#include "config/nvs_store.h"
#include "net/mqtt_client.h"

namespace cli {

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
    if (identity::setDeviceCode(val)) {
      Serial.println("[cli] deviceCode saved → hot reloaded");
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
