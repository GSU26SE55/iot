#include "config/runtime_config.h"

#include <Arduino.h>
#include <cstring>

#include "config/nvs_store.h"

#if __has_include("config.h")
  #include "config.h"
#else
  #include "config.example.h"
#endif

namespace runtimecfg {
namespace {

constexpr const char* kWifiSsidKey = "wifiSsid";
constexpr const char* kWifiPasswordKey = "wifiPass";
constexpr const char* kBackendUrlKey = "backendUrl";
constexpr const char* kMqttHostKey = "mqttHost";
constexpr const char* kMqttPortKey = "mqttPort";
constexpr const char* kMqttTlsKey = "mqttTls";
constexpr const char* kMqttUsernameKey = "mqttUser";
constexpr const char* kMqttPasswordKey = "mqttPass";

RuntimeConfig s_config{};
bool s_hasStoredWifi = false;

void copySafe(char* destination, size_t destinationLen, const char* source) {
  if (destination == nullptr || destinationLen == 0) return;
  if (source == nullptr) {
    destination[0] = '\0';
    return;
  }
  strncpy(destination, source, destinationLen - 1);
  destination[destinationLen - 1] = '\0';
}

bool loadString(const char* key, char* destination, size_t destinationLen,
                const char* fallback) {
  if (storage::nvsGetString(key, destination, destinationLen)) return true;
  copySafe(destination, destinationLen, fallback);
  return false;
}

}  // namespace

void runtimeConfigBegin() {
  const bool ssidFromNvs = loadString(kWifiSsidKey, s_config.wifiSsid,
                                      sizeof(s_config.wifiSsid), WIFI_SSID);
  loadString(kWifiPasswordKey, s_config.wifiPassword,
             sizeof(s_config.wifiPassword), WIFI_PASS);
  const bool backendFromNvs = loadString(kBackendUrlKey, s_config.backendUrl,
                                         sizeof(s_config.backendUrl), BACKEND_URL);
  const bool mqttHostFromNvs = loadString(kMqttHostKey, s_config.mqttHost,
                                          sizeof(s_config.mqttHost), MQTT_BROKER_HOST);
  loadString(kMqttUsernameKey, s_config.mqttUsername,
             sizeof(s_config.mqttUsername), MQTT_USERNAME);
  loadString(kMqttPasswordKey, s_config.mqttPassword,
             sizeof(s_config.mqttPassword), MQTT_PASSWORD);

  int32_t port = storage::nvsGetInt32(kMqttPortKey, MQTT_BROKER_PORT);
  if (port < 1 || port > 65535) port = MQTT_BROKER_PORT;
  s_config.mqttPort = static_cast<uint16_t>(port);
  s_config.mqttUseTls = storage::nvsGetUInt8(kMqttTlsKey, MQTT_USE_TLS ? 1 : 0) != 0;
  s_hasStoredWifi = ssidFromNvs;

  Serial.printf("[config] wifi=%s (source=%s)\n", s_config.wifiSsid,
                ssidFromNvs ? "NVS" : "config.h");
  Serial.printf("[config] backend=%s (source=%s)\n", s_config.backendUrl,
                backendFromNvs ? "NVS" : "config.h");
  Serial.printf("[config] mqtt=%s:%u tls=%d (source=%s)\n",
                s_config.mqttHost, static_cast<unsigned>(s_config.mqttPort),
                s_config.mqttUseTls ? 1 : 0,
                mqttHostFromNvs ? "NVS" : "config.h");
}

const RuntimeConfig& runtimeConfig() {
  return s_config;
}

bool saveRuntimeConfig(const RuntimeConfig& config) {
  bool ok = true;
  ok = storage::nvsPutString(kWifiSsidKey, config.wifiSsid) && ok;
  ok = storage::nvsPutString(kWifiPasswordKey, config.wifiPassword) && ok;
  ok = storage::nvsPutString(kBackendUrlKey, config.backendUrl) && ok;
  ok = storage::nvsPutString(kMqttHostKey, config.mqttHost) && ok;
  ok = storage::nvsPutInt32(kMqttPortKey, config.mqttPort) && ok;
  ok = storage::nvsPutUInt8(kMqttTlsKey, config.mqttUseTls ? 1 : 0) && ok;
  ok = storage::nvsPutString(kMqttUsernameKey, config.mqttUsername) && ok;
  ok = storage::nvsPutString(kMqttPasswordKey, config.mqttPassword) && ok;
  if (!ok) {
    Serial.println("[config] save FAILED");
    return false;
  }

  s_config = config;
  s_hasStoredWifi = true;
  Serial.println("[config] runtime configuration saved to NVS");
  return true;
}

bool hasStoredWifiConfig() {
  return s_hasStoredWifi;
}

}  // namespace runtimecfg
