#include "net/host_resolver.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <cstring>

#include "core/net_config_rules.h"

namespace net {
namespace {

constexpr uint32_t kSuccessCacheMs = 60000UL;
constexpr uint32_t kFailureCacheMs = 5000UL;
constexpr uint32_t kMdnsTimeoutMs = 1800UL;
constexpr size_t kHostBufferLen = 96;

char s_cachedHost[kHostBufferLen]{};
IPAddress s_cachedAddress;
IPAddress s_stationAddress;
uint32_t s_cachedAtMs = 0;
bool s_cacheValid = false;
bool s_cacheSucceeded = false;

bool sameHost(const char* host) {
  return host != nullptr && strncmp(s_cachedHost, host, sizeof(s_cachedHost)) == 0;
}

void remember(const char* host, const IPAddress& address, bool succeeded) {
  snprintf(s_cachedHost, sizeof(s_cachedHost), "%s", host ? host : "");
  s_cachedAddress = address;
  s_stationAddress = WiFi.localIP();
  s_cachedAtMs = millis();
  s_cacheValid = true;
  s_cacheSucceeded = succeeded;
}

}  // namespace

bool resolveMdnsHost(const char* host, IPAddress& outAddress) {
  if (!core::isMdnsHostname(host) || WiFi.status() != WL_CONNECTED) return false;

  const uint32_t now = millis();
  if (s_cacheValid && sameHost(host) && s_stationAddress == WiFi.localIP()) {
    const uint32_t ttl = s_cacheSucceeded ? kSuccessCacheMs : kFailureCacheMs;
    if (now - s_cachedAtMs < ttl) {
      if (s_cacheSucceeded) outAddress = s_cachedAddress;
      return s_cacheSucceeded;
    }
  }

  char label[kHostBufferLen];
  if (core::mdnsQueryLabel(host, label, sizeof(label)) == 0) return false;

  // ESPmDNS yêu cầu label không có hậu tố `.local`.
  IPAddress resolved = MDNS.queryHost(label, kMdnsTimeoutMs);

  // Một số Arduino-ESP32 build đã bật LWIP_DNS_SUPPORT_MDNS_QUERIES. Giữ
  // hostByName làm fallback để dùng được cả implementation đó lẫn responder MDNS.
  if (static_cast<uint32_t>(resolved) == 0) {
    WiFi.hostByName(host, resolved);
  }

  const bool ok = static_cast<uint32_t>(resolved) != 0;
  remember(host, resolved, ok);
  if (ok) {
    outAddress = resolved;
    Serial.printf("[resolver] mDNS %s -> %s\n", host, resolved.toString().c_str());
  } else {
    Serial.printf("[resolver] mDNS khong tim thay %s; se thu lai\n", host);
  }
  return ok;
}

}  // namespace net
