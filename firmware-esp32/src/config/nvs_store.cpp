// ==================================================================
// Sprint 2 — S2-FW-01: NVS wrapper implementation
//
// Dùng Arduino-ESP32 `Preferences` class — wrap NVS partition.
// ==================================================================
#include "config/nvs_store.h"

#include <Arduino.h>
#include <Preferences.h>

namespace storage {

namespace {
constexpr const char* kNamespace = "iot";

// 1 Preferences instance dùng chung — open/close per gọi để tránh giữ handle.
Preferences s_prefs;

bool openRW() {
  // false = read-write
  return s_prefs.begin(kNamespace, false);
}

bool openRO() {
  // true = read-only
  return s_prefs.begin(kNamespace, true);
}
}  // namespace

bool nvsBegin() {
  // RW mode để TẠO namespace nếu chưa tồn tại (Preferences::begin(name, false)).
  // Nếu dùng RO trên device fresh, namespace "iot" chưa có → begin trả false →
  // mọi op subsequent silent fail.
  if (!openRW()) {
    Serial.println("[nvs] mount FAILED (NVS partition có thể corrupt — try erase)");
    return false;
  }
  s_prefs.end();
  Serial.println("[nvs] mount OK namespace=\"iot\"");
  return true;
}

bool nvsErase() {
  if (!openRW()) return false;
  bool ok = s_prefs.clear();
  s_prefs.end();
  if (ok) Serial.println("[nvs] erased namespace \"iot\"");
  return ok;
}

bool nvsGetString(const char* key, char* outBuf, size_t outLen) {
  if (outBuf == nullptr || outLen == 0) return false;
  outBuf[0] = '\0';
  if (!openRO()) return false;

  bool exists = s_prefs.isKey(key);
  if (!exists) {
    s_prefs.end();
    return false;
  }
  size_t n = s_prefs.getString(key, outBuf, outLen);
  s_prefs.end();
  return n > 0;
}

bool nvsPutString(const char* key, const char* value) {
  if (key == nullptr || value == nullptr) return false;
  if (!openRW()) return false;
  size_t n = s_prefs.putString(key, value);
  s_prefs.end();
  return n > 0;
}

int32_t nvsGetInt32(const char* key, int32_t fallback) {
  if (!openRO()) return fallback;
  int32_t v = s_prefs.getInt(key, fallback);
  s_prefs.end();
  return v;
}

bool nvsPutInt32(const char* key, int32_t value) {
  if (!openRW()) return false;
  size_t n = s_prefs.putInt(key, value);
  s_prefs.end();
  return n > 0;
}

uint8_t nvsGetUInt8(const char* key, uint8_t fallback) {
  if (!openRO()) return fallback;
  uint8_t v = s_prefs.getUChar(key, fallback);
  s_prefs.end();
  return v;
}

bool nvsPutUInt8(const char* key, uint8_t value) {
  if (!openRW()) return false;
  size_t n = s_prefs.putUChar(key, value);
  s_prefs.end();
  return n > 0;
}

bool nvsHasKey(const char* key) {
  if (!openRO()) return false;
  bool exists = s_prefs.isKey(key);
  s_prefs.end();
  return exists;
}

size_t nvsFreeEntries() {
  if (!openRO()) return 0;
  size_t free = s_prefs.freeEntries();
  s_prefs.end();
  return free;
}

}  // namespace storage
