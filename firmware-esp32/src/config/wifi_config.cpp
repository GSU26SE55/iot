// ==================================================================
// IOT3-36 — hiện thực WiFi runtime store. Xem wifi_config.h.
// ==================================================================
#include "config/wifi_config.h"

#include <Arduino.h>
#include <string.h>

#include "config/nvs_store.h"

#if __has_include("config.h")
  #include "config.h"
#else
  #include "config.example.h"
#endif

namespace wificfg {

namespace {

// ⚠️ Khoá NVS tối đa 15 ký tự (Preferences spec) — hai khoá dưới đều 8 ký tự.
constexpr const char* kKeySsid = "wifissid";
constexpr const char* kKeyPass = "wifipass";

char s_ssid[kSsidBufLen];
char s_pass[kPassBufLen];
bool s_fromNvs = false;

void copySafe(char* dst, size_t dstLen, const char* src) {
  if (dst == nullptr || dstLen == 0) return;
  if (src == nullptr) { dst[0] = '\0'; return; }
  strncpy(dst, src, dstLen - 1);
  dst[dstLen - 1] = '\0';
}

/// <summary>Nạp một trường từ NVS, chỉ nhận khi giá trị còn NGUYÊN VẸN.</summary>
/// <remarks>
/// Buffer đọc phải ĐÚNG cỡ buffer đích. `nvsGetString` trả false khi chuỗi không vừa — đó chính
/// là dấu hiệu giá trị quá khổ do bản firmware cũ ghi vào. Im lặng quay về compile-time ở đây sẽ
/// khiến người vận hành thấy thiết bị tự đổi mạng mà không hiểu vì sao, nên phải nói ra.
/// </remarks>
bool loadValidated(const char* key, char* dst, size_t dstLen,
                   const char* fallback, size_t maxChars, bool allowEmpty, const char* label) {
  if (!storage::nvsGetString(key, dst, dstLen)) {
    if (storage::nvsHasKey(key)) {
      Serial.printf("[wificfg] BỎ %s trong NVS — không đọc được (nhiều khả năng quá dài, tối đa "
                    "%u ký tự); dùng mặc định compile-time.\n",
                    label, static_cast<unsigned>(maxChars));
    }
    copySafe(dst, dstLen, fallback);
    return false;
  }

  const auto err = core::validateWifiField(dst, maxChars, allowEmpty);
  if (err != core::WifiFieldError::Ok) {
    Serial.printf("[wificfg] BỎ %s trong NVS — %s; dùng mặc định compile-time.\n",
                  label, core::describeWifiError(err));
    copySafe(dst, dstLen, fallback);
    return false;
  }
  return true;
}

}  // namespace

void begin() {
  const bool ssidFromNvs =
      loadValidated(kKeySsid, s_ssid, sizeof(s_ssid), WIFI_SSID,
                    core::kMaxWifiSsidChars, /*allowEmpty=*/false, "SSID");
  const bool passFromNvs =
      loadValidated(kKeyPass, s_pass, sizeof(s_pass), WIFI_PASS,
                    core::kMaxWifiPassChars, /*allowEmpty=*/true, "mật khẩu");

  // Chỉ tính là "từ NVS" khi SSID đến từ NVS. Mật khẩu rỗng của mạng mở không tự nó
  // đủ để kết luận, và một cặp lai (SSID compile-time + mật khẩu NVS) gần như chắc chắn
  // là cấu hình hỏng — báo ra để người lắp đặt biết đường xoá đi làm lại.
  s_fromNvs = ssidFromNvs;
  if (passFromNvs && !ssidFromNvs) {
    Serial.println("[wificfg] ⚠ NVS có mật khẩu nhưng KHÔNG có SSID hợp lệ — "
                   "cấu hình lai, hãy đặt lại cả hai (`set wifi <ssid> <pass>` hoặc trang setup).");
  }

  Serial.printf("[wificfg] ssid=\"%s\" (nguồn=%s) mật khẩu=%s\n",
                s_ssid, s_fromNvs ? "NVS" : "compile-time",
                s_pass[0] == '\0' ? "(rỗng — mạng mở)" : "***");
}

const char* ssid()     { return s_ssid; }
const char* password() { return s_pass; }
bool isFromNvs()       { return s_fromNvs; }
bool isConfigured()    { return core::wifiConfigUsable(s_ssid); }

bool save(const char* newSsid, const char* newPassword) {
  const auto ssidErr = core::validateWifiField(newSsid, core::kMaxWifiSsidChars, false);
  if (ssidErr != core::WifiFieldError::Ok) {
    Serial.printf("[wificfg] TỪ CHỐI SSID — %s\n", core::describeWifiError(ssidErr));
    return false;
  }
  // `nullptr` được coi là mật khẩu rỗng (mạng mở), không phải lỗi.
  const char* pw = newPassword == nullptr ? "" : newPassword;
  const auto passErr = core::validateWifiField(pw, core::kMaxWifiPassChars, true);
  if (passErr != core::WifiFieldError::Ok) {
    Serial.printf("[wificfg] TỪ CHỐI mật khẩu — %s\n", core::describeWifiError(passErr));
    return false;
  }

  // Ghi SSID trước: nếu mất điện giữa chừng thì thà có SSID mà sai mật khẩu (thiết bị mở lại
  // trang setup sau 5 phút) còn hơn có mật khẩu mồ côi không biết của mạng nào.
  if (!storage::nvsPutString(kKeySsid, newSsid)) return false;
  if (!storage::nvsPutString(kKeyPass, pw))      return false;

  copySafe(s_ssid, sizeof(s_ssid), newSsid);
  copySafe(s_pass, sizeof(s_pass), pw);
  s_fromNvs = true;
  Serial.printf("[wificfg] đã lưu ssid=\"%s\" vào NVS\n", s_ssid);
  return true;
}

bool clear() {
  const bool a = storage::nvsPutString(kKeySsid, "");
  const bool b = storage::nvsPutString(kKeyPass, "");
  copySafe(s_ssid, sizeof(s_ssid), WIFI_SSID);
  copySafe(s_pass, sizeof(s_pass), WIFI_PASS);
  s_fromNvs = false;
  Serial.println("[wificfg] đã xoá cấu hình WiFi trong NVS — quay về compile-time");
  return a && b;
}

void printStatus() {
  Serial.println("==== WiFi config ====");
  Serial.printf("  ssid      = \"%s\" (%s)\n", s_ssid, s_fromNvs ? "NVS" : "compile-time");
  Serial.printf("  mật khẩu  = %s\n", s_pass[0] == '\0' ? "(rỗng — mạng mở)" : "***");
  Serial.printf("  usable    = %s\n", isConfigured() ? "có" : "KHÔNG");
  Serial.println("=====================");
}

}  // namespace wificfg
