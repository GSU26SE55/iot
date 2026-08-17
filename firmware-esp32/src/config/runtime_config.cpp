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

constexpr const char* kBackendUrlKey = "backendUrl";

char s_backendUrl[kMaxBackendUrlLen]{};

void copySafe(char* destination, size_t destinationLen, const char* source) {
  if (destination == nullptr || destinationLen == 0) return;
  if (source == nullptr) {
    destination[0] = '\0';
    return;
  }
  strncpy(destination, source, destinationLen - 1);
  destination[destinationLen - 1] = '\0';
}

}  // namespace

void runtimeConfigBegin() {
  const bool backendFromNvs =
      storage::nvsGetString(kBackendUrlKey, s_backendUrl, sizeof(s_backendUrl));
  if (!backendFromNvs) copySafe(s_backendUrl, sizeof(s_backendUrl), BACKEND_URL);
  Serial.printf("[config] backend=%s (source=%s)\n", s_backendUrl,
                backendFromNvs ? "NVS" : "config.h");
}

const char* backendUrl() {
  return s_backendUrl;
}

bool saveBackendUrl(const char* url) {
  if (url == nullptr || url[0] == '\0' || strlen(url) >= sizeof(s_backendUrl) ||
      (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0)) {
    Serial.println("[config] backend URL invalid");
    return false;
  }
  if (!storage::nvsPutString(kBackendUrlKey, url)) return false;
  copySafe(s_backendUrl, sizeof(s_backendUrl), url);
  Serial.printf("[config] backend URL saved: %s\n", s_backendUrl);
  return true;
}

}  // namespace runtimecfg
