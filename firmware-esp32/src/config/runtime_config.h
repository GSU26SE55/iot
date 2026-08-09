#pragma once

#include <cstddef>

namespace runtimecfg {

constexpr size_t kMaxBackendUrlLen = 160;

// Load the backend URL saved by the setup portal. Wi-Fi and MQTT have their
// own runtime stores (wificfg and mqttcfg) and must not be duplicated here.
// Call after storage::nvsBegin().
void runtimeConfigBegin();

const char* backendUrl();

// Persist a validated absolute HTTP(S) base URL and update the RAM cache.
bool saveBackendUrl(const char* url);

}  // namespace runtimecfg
