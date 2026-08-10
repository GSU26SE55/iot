// ==================================================================
// Local setup access point.
//
// The AP is deliberately always available. A provisioned gateway therefore
// remains configurable after the customer changes router/house; firmware
// reflashing and a factory reset are not part of the normal Wi-Fi flow.
// ==================================================================
#include "net/setup_portal.h"

#include <Arduino.h>
#include <WiFi.h>
#include <cstring>

#if __has_include("config.h")
  #include "config.h"
#else
  #include "config.example.h"
#endif

#ifndef SETUP_AP_PASSWORD
  #define SETUP_AP_PASSWORD CONFIG_PORTAL_PASSWORD
#endif

namespace net {
namespace {

PortalMode s_mode = PortalMode::Off;
bool s_active = false;
uint32_t s_startedMs = 0;
char s_apSsid[24] = {};

void buildApSsid() {
  if (s_apSsid[0] != '\0') return;

  const String mac = WiFi.macAddress();
  char tail[5] = {'0', '0', '0', '0', '\0'};
  const int length = mac.length();
  if (length >= 5) {
    tail[0] = mac[length - 5];
    tail[1] = mac[length - 4];
    tail[2] = mac[length - 2];
    tail[3] = mac[length - 1];
  }
  snprintf(s_apSsid, sizeof(s_apSsid), "SolarGW-%s", tail);
}

bool startSoftAp() {
  // Set AP+STA before softAP(). Reversing this order can silently tear down
  // the just-created SoftAP on ESP32-S3 while softAPIP() still looks valid.
  if (WiFi.getMode() != WIFI_AP_STA && !WiFi.mode(WIFI_AP_STA)) {
    Serial.println("[portal] cannot switch Wi-Fi radio to AP+STA");
    return false;
  }

  delay(20);
  if (!WiFi.softAP(s_apSsid, SETUP_AP_PASSWORD)) {
    Serial.println("[portal] cannot start setup SoftAP");
    return false;
  }
  return true;
}

}  // namespace

const char* portalApSsid() {
  buildApSsid();
  return s_apSsid;
}

bool portalStart(PortalMode mode) {
  if (mode == PortalMode::Off) return false;
  if (s_active) return true;

  const size_t passwordLength = strlen(SETUP_AP_PASSWORD);
  if (passwordLength < 8) {
    Serial.printf("[portal] AP password has %u characters; WPA2 requires at least 8\n",
                  static_cast<unsigned>(passwordLength));
    return false;
  }

  buildApSsid();
  if (!startSoftAp()) return false;

  s_active = true;
  s_mode = mode;
  s_startedMs = millis();

  Serial.println("========================================");
  Serial.println(" SETUP WEB IS ALWAYS AVAILABLE");
  Serial.printf("   Wi-Fi:    %s\n", s_apSsid);
  Serial.printf("   Password: %s\n", SETUP_AP_PASSWORD);
  Serial.println("   URL:      http://192.168.4.1:8080");
  Serial.println("========================================");
  return true;
}

void portalTick() {
  if (!s_active) return;

  // A driver recovery or a future Wi-Fi change must not permanently remove
  // the setup entry point. Only rebuild the AP if its radio bit was lost.
  if ((WiFi.getMode() & WIFI_AP) == 0) {
    Serial.println("[portal] AP radio was lost; restoring setup AP");
    startSoftAp();
  }
}

void portalStop() {
  if (!s_active) return;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  s_active = false;
  s_mode = PortalMode::Off;
  Serial.println("[portal] setup AP stopped");
}

bool portalIsActive() { return s_active; }
PortalMode portalMode() { return s_mode; }
uint32_t portalStartedAtMs() { return s_startedMs; }

}  // namespace net
