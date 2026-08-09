#pragma once

#include <cstddef>
#include <cstdint>

namespace runtimecfg {

constexpr size_t kMaxWifiSsidLen = 33;
constexpr size_t kMaxWifiPasswordLen = 65;
constexpr size_t kMaxBackendUrlLen = 160;
constexpr size_t kMaxMqttHostLen = 96;
constexpr size_t kMaxMqttUsernameLen = 65;
constexpr size_t kMaxMqttPasswordLen = 97;

struct RuntimeConfig {
  char wifiSsid[kMaxWifiSsidLen];
  char wifiPassword[kMaxWifiPasswordLen];
  char backendUrl[kMaxBackendUrlLen];
  char mqttHost[kMaxMqttHostLen];
  uint16_t mqttPort;
  bool mqttUseTls;
  char mqttUsername[kMaxMqttUsernameLen];
  char mqttPassword[kMaxMqttPasswordLen];
};

// Load values saved by the setup portal. Missing values fall back to config.h.
// Call after storage::nvsBegin().
void runtimeConfigBegin();

const RuntimeConfig& runtimeConfig();

// Persist a complete validated snapshot and update the in-memory copy.
bool saveRuntimeConfig(const RuntimeConfig& config);

// True once Wi-Fi credentials have been explicitly saved through the portal.
bool hasStoredWifiConfig();

}  // namespace runtimecfg
