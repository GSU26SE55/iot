// ==================================================================
// IOT3-49 — hiện thực bảng ánh xạ pin runtime. Xem battery_map_runtime.h.
// ==================================================================
#include "config/battery_map_runtime.h"

#include <Arduino.h>
#include <string.h>

#include "config/battery_mapping.h"
#include "config/nvs_store.h"
#include "core/source_tags.h"

namespace batmap {

namespace {

// ⚠️ Khoá NVS tối đa 15 ký tự (Preferences spec).
constexpr const char* kKeyMap = "batmap";

/// 8 mục × (39 serial + 1 + 3 unitId + 1 + 23 code + 1) ≈ 544 ⇒ lấy 640 cho dư.
constexpr size_t kEncodedBufLen = 640;

core::BatteryMapEntry s_entries[core::kMaxBatteryMapEntries];
size_t s_count   = 0;
bool   s_fromNvs = false;

void copySafe(char* dst, size_t dstLen, const char* src) {
  if (dst == nullptr || dstLen == 0) return;
  if (src == nullptr) { dst[0] = '\0'; return; }
  strncpy(dst, src, dstLen - 1);
  dst[dstLen - 1] = '\0';
}

/// Dựng bảng từ `config::kBatteryMappings`.
void loadCompileTimeTable() {
  s_count = 0;
  for (size_t i = 0; i < config::kBatteryMappingsCount && s_count < core::kMaxBatteryMapEntries; ++i) {
    const auto& m = config::kBatteryMappings[i];
    if (core::validateBatteryMapEntry(m.serial, m.unitId, core::kSourceCodePrimary)
        != core::BatteryMapError::Ok) {
      Serial.printf("[batmap] BỎ dòng bảng cứng #%u (serial=%s unitId=%u) — không hợp lệ\n",
                    static_cast<unsigned>(i), m.serial, static_cast<unsigned>(m.unitId));
      continue;
    }
    core::BatteryMapEntry& e = s_entries[s_count++];
    copySafe(e.serial, sizeof(e.serial), m.serial);
    copySafe(e.sensorSourceCode, sizeof(e.sensorSourceCode), core::kSourceCodePrimary);
    e.unitId = m.unitId;
  }
  s_fromNvs = false;
}

}  // namespace

void begin() {
  char buf[kEncodedBufLen];
  if (storage::nvsGetString(kKeyMap, buf, sizeof(buf)) && buf[0] != '\0') {
    const size_t n = core::decodeBatteryMap(buf, s_entries, core::kMaxBatteryMapEntries);
    if (n > 0) {
      s_count   = n;
      s_fromNvs = true;
      Serial.printf("[batmap] nạp %u pin từ NVS\n", static_cast<unsigned>(s_count));
      return;
    }
    Serial.println("[batmap] chuỗi trong NVS không giải được mục nào — dùng bảng cứng");
  } else if (storage::nvsHasKey(kKeyMap)) {
    Serial.println("[batmap] không đọc được khoá 'batmap' (quá dài?) — dùng bảng cứng");
  }

  loadCompileTimeTable();
  Serial.printf("[batmap] dùng bảng cứng compile-time (%u pin)\n",
                static_cast<unsigned>(s_count));
}

size_t count() { return s_count; }
bool   isFromNvs() { return s_fromNvs; }

const core::BatteryMapEntry* at(size_t i) {
  return i < s_count ? &s_entries[i] : nullptr;
}

const core::BatteryMapEntry* findBySerial(const char* serial) {
  if (serial == nullptr || serial[0] == '\0') return nullptr;
  for (size_t i = 0; i < s_count; ++i) {
    if (strcmp(s_entries[i].serial, serial) == 0) return &s_entries[i];
  }
  return nullptr;
}

const core::BatteryMapEntry* findByUnitId(uint8_t unitId) {
  for (size_t i = 0; i < s_count; ++i) {
    if (s_entries[i].unitId == unitId) return &s_entries[i];
  }
  return nullptr;
}

uint8_t unitIdForSerial(const char* serial) {
  const auto* e = findBySerial(serial);
  return e != nullptr ? e->unitId : 0;
}

const char* serialForUnitId(uint8_t unitId) {
  const auto* e = findByUnitId(unitId);
  return e != nullptr ? e->serial : nullptr;
}

const char* assetIdForSerial(const char* serial) {
  if (serial == nullptr || serial[0] == '\0') return "";
  for (const auto& m : config::kBatteryMappings) {
    if (strcmp(m.serial, serial) == 0) return m.batteryAssetId;
  }
  return "";
}

bool applyFromProvision(const core::BatteryMapEntry* entries, size_t n) {
  if (entries == nullptr || n == 0) {
    Serial.println("[batmap] provision không kèm pin nào — giữ nguyên bảng đang dùng");
    return false;
  }
  if (n > core::kMaxBatteryMapEntries) {
    Serial.printf("[batmap] ⚠ backend gửi %u pin, firmware chỉ ôm được %u — LẤY %u PIN ĐẦU, "
                  "phần còn lại sẽ KHÔNG được đọc.\n",
                  static_cast<unsigned>(n),
                  static_cast<unsigned>(core::kMaxBatteryMapEntries),
                  static_cast<unsigned>(core::kMaxBatteryMapEntries));
    n = core::kMaxBatteryMapEntries;
  }

  // Có khác bảng đang dùng không? Giống hệt thì đừng ghi NVS — mỗi lần ghi là một chu kỳ
  // xoá/ghi flash, mà provision chạy mỗi lần boot.
  bool changed = (n != s_count);
  if (!changed) {
    for (size_t i = 0; i < n; ++i) {
      if (strcmp(entries[i].serial, s_entries[i].serial) != 0 ||
          strcmp(entries[i].sensorSourceCode, s_entries[i].sensorSourceCode) != 0 ||
          entries[i].unitId != s_entries[i].unitId) {
        changed = true;
        break;
      }
    }
  }
  if (!changed && s_fromNvs) {
    Serial.printf("[batmap] bảng %u pin từ provision trùng bảng đang dùng — không ghi\n",
                  static_cast<unsigned>(n));
    return false;
  }

  char buf[kEncodedBufLen];
  const size_t written = core::encodeBatteryMap(entries, n, buf, sizeof(buf));
  if (written == 0) {
    Serial.println("[batmap] TỪ CHỐI — bảng không vừa buffer mã hoá, giữ nguyên bảng cũ");
    return false;
  }
  if (!storage::nvsPutString(kKeyMap, buf)) {
    Serial.println("[batmap] TỪ CHỐI — ghi NVS thất bại, giữ nguyên bảng cũ");
    return false;
  }

  for (size_t i = 0; i < n; ++i) s_entries[i] = entries[i];
  s_count   = n;
  s_fromNvs = true;
  Serial.printf("[batmap] áp bảng mới từ provision — %u pin\n", static_cast<unsigned>(s_count));
  printStatus();
  return true;
}

bool clear() {
  const bool ok = storage::nvsPutString(kKeyMap, "");
  loadCompileTimeTable();
  Serial.println("[batmap] đã xoá bảng trong NVS — quay về bảng cứng");
  return ok;
}

void printStatus() {
  Serial.printf("==== battery map (%s, %u pin) ====\n",
                s_fromNvs ? "NVS" : "compile-time", static_cast<unsigned>(s_count));
  for (size_t i = 0; i < s_count; ++i) {
    Serial.printf("  [%u] serial=%s unitId=%u source=%s\n",
                  static_cast<unsigned>(i), s_entries[i].serial,
                  static_cast<unsigned>(s_entries[i].unitId),
                  s_entries[i].sensorSourceCode[0] == '\0' ? "(mặc định)"
                                                           : s_entries[i].sensorSourceCode);
  }
  Serial.println("=========================================");
}

}  // namespace batmap
