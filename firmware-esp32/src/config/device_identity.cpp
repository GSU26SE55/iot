// ==================================================================
// Sprint 2 — S2-FW-01: Device identity store implementation
// ==================================================================
#include "config/device_identity.h"

#include <Arduino.h>
#include <string.h>

#include "config/nvs_store.h"

#if __has_include("config.h")
  #include "config.h"
#else
  #include "config.example.h"
#endif

namespace identity {

namespace {
constexpr const char* kKeyApiKey     = "apikey";
constexpr const char* kKeyDeviceCode = "devcode";

char s_apiKey    [kMaxApiKeyLen];
char s_deviceCode[kMaxDeviceCodeLen];
bool s_apiKeyFromNvs     = false;
bool s_deviceCodeFromNvs = false;

void copySafe(char* dst, size_t dstLen, const char* src) {
  if (dst == nullptr || dstLen == 0) return;
  if (src == nullptr) { dst[0] = '\0'; return; }
  strncpy(dst, src, dstLen - 1);
  dst[dstLen - 1] = '\0';
}
}  // namespace

void identityBegin() {
  // 1. Try NVS
  char buf[kMaxApiKeyLen];
  if (storage::nvsGetString(kKeyApiKey, buf, sizeof(buf))) {
    copySafe(s_apiKey, sizeof(s_apiKey), buf);
    s_apiKeyFromNvs = true;
  } else {
    copySafe(s_apiKey, sizeof(s_apiKey), API_KEY);
    s_apiKeyFromNvs = false;
  }

  if (storage::nvsGetString(kKeyDeviceCode, buf, sizeof(buf))) {
    copySafe(s_deviceCode, sizeof(s_deviceCode), buf);
    s_deviceCodeFromNvs = true;
  } else {
    copySafe(s_deviceCode, sizeof(s_deviceCode), DEVICE_CODE);
    s_deviceCodeFromNvs = false;
  }

  Serial.printf("[identity] deviceCode=%s (source=%s)\n",
                s_deviceCode, s_deviceCodeFromNvs ? "NVS" : "compile-time");
  char masked[24];
  maskedApiKey(masked, sizeof(masked));
  Serial.printf("[identity] apiKey=%s (source=%s)\n",
                masked, s_apiKeyFromNvs ? "NVS" : "compile-time");
}

const char* apiKey()     { return s_apiKey;     }
const char* deviceCode() { return s_deviceCode; }

bool setApiKey(const char* newKey) {
  if (newKey == nullptr || newKey[0] == '\0') return false;
  if (strlen(newKey) >= kMaxApiKeyLen) {
    Serial.printf("[identity] apiKey quá dài (%u >= %u) — truncate\n",
                  static_cast<unsigned>(strlen(newKey)),
                  static_cast<unsigned>(kMaxApiKeyLen));
  }
  if (!storage::nvsPutString(kKeyApiKey, newKey)) return false;
  copySafe(s_apiKey, sizeof(s_apiKey), newKey);
  s_apiKeyFromNvs = true;
  Serial.println("[identity] apiKey updated (NVS)");
  return true;
}

bool setDeviceCode(const char* newCode) {
  if (newCode == nullptr || newCode[0] == '\0') return false;
  if (strlen(newCode) >= kMaxDeviceCodeLen) {
    Serial.printf("[identity] deviceCode quá dài (%u >= %u) — truncate\n",
                  static_cast<unsigned>(strlen(newCode)),
                  static_cast<unsigned>(kMaxDeviceCodeLen));
  }
  if (!storage::nvsPutString(kKeyDeviceCode, newCode)) return false;
  copySafe(s_deviceCode, sizeof(s_deviceCode), newCode);
  s_deviceCodeFromNvs = true;
  Serial.printf("[identity] deviceCode updated → %s\n", s_deviceCode);
  return true;
}

bool resetToDefaults() {
  bool ok = storage::nvsErase();
  copySafe(s_apiKey,     sizeof(s_apiKey),     API_KEY);
  copySafe(s_deviceCode, sizeof(s_deviceCode), DEVICE_CODE);
  s_apiKeyFromNvs     = false;
  s_deviceCodeFromNvs = false;
  Serial.println("[identity] reset to compile-time defaults");
  return ok;
}

void maskedApiKey(char* outBuf, size_t outBufLen) {
  if (outBuf == nullptr || outBufLen == 0) return;
  size_t n = strlen(s_apiKey);
  if (n <= 8) {
    // Short key — chỉ mask hoàn toàn
    snprintf(outBuf, outBufLen, "***");
    return;
  }
  // first 4 + "***" + last 4
  snprintf(outBuf, outBufLen, "%.4s***%s", s_apiKey, s_apiKey + n - 4);
}

void printStatus() {
  char masked[32];
  maskedApiKey(masked, sizeof(masked));
  Serial.println("==== Identity status ====");
  Serial.printf("  deviceCode = %s (%s)\n", s_deviceCode,
                s_deviceCodeFromNvs ? "NVS" : "compile-time fallback");
  Serial.printf("  apiKey     = %s (%s)\n", masked,
                s_apiKeyFromNvs ? "NVS" : "compile-time fallback");
  Serial.println("=========================");
}

}  // namespace identity
