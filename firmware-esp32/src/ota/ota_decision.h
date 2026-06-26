// ==================================================================
// Sprint 7 — S7-FW-01 (#73): OTA decision logic — pure (header-only).
//
// Tách logic thuần khỏi ota_update.cpp (Arduino/IDF deps) để test native:
//   - otaShouldUpdate: có nên apply update không (khớp semantics backend
//     CheckIotFirmwareUpdateQueryHandler — so version bằng CHUỖI tuyệt đối).
//   - otaSha256Equal: so SHA-256 hex (case-insensitive, đúng 64 hex char).
//
// Pure C++ — không phụ thuộc Arduino (giống incident_trigger.h, cmd_logic.h).
// Test: test_ota_decision.
// ==================================================================
#pragma once

#include <cstddef>
#include <cstring>

namespace ota {

// Backend offer update khi target.Version != currentVersion (StringComparison.Ordinal)
// VÀ hasUpdate=true. FW chỉ apply khi cả 2 đúng + có targetVer/sha hợp lệ.
// (null/empty currentVer vẫn hợp lệ — coi như "" khác mọi target không rỗng.)
inline bool otaShouldUpdate(bool hasUpdate, const char* currentVer, const char* targetVer) {
  if (!hasUpdate) return false;
  if (targetVer == nullptr || targetVer[0] == '\0') return false;     // không có version đích
  const char* cur = (currentVer == nullptr) ? "" : currentVer;
  return std::strcmp(cur, targetVer) != 0;                            // khác chuỗi → update
}

// So 2 chuỗi SHA-256 hex (64 ký tự) — KHÔNG phân biệt hoa/thường.
// Trả false nếu null/empty/độ dài khác 64 (defensive — backend luôn trả 64 hex).
inline bool otaSha256Equal(const char* a, const char* b) {
  if (a == nullptr || b == nullptr) return false;
  size_t la = std::strlen(a), lb = std::strlen(b);
  if (la != 64 || lb != 64) return false;
  for (size_t i = 0; i < 64; ++i) {
    char ca = a[i], cb = b[i];
    if (ca >= 'A' && ca <= 'F') ca = static_cast<char>(ca - 'A' + 'a');
    if (cb >= 'A' && cb <= 'F') cb = static_cast<char>(cb - 'A' + 'a');
    if (ca != cb) return false;
  }
  return true;
}

}  // namespace ota
