// ==================================================================
// Sprint 2 — S2-FW-01: Device identity store implementation
// ==================================================================
#include "config/device_identity.h"

#include <Arduino.h>
#include <string.h>

#include "config/nvs_store.h"
#include "core/identity_change_policy.h"  // core::kMaxDeviceCodeLen — chốt biên bên dưới
#include "core/identity_validation.h"

#if __has_include("config.h")
  #include "config.h"
#else
  #include "config.example.h"
#endif

namespace identity {

// GH-749 — buffer lưu trữ phải chứa lọt MỌI giá trị mà luật đổi mã cho là hợp lệ. Không có
// dòng này thì hai hằng dễ trôi khỏi nhau, và hậu quả là CLI duyệt xong nhưng ghi lại hỏng.
static_assert(kMaxDeviceCodeLen >= core::kMaxDeviceCodeLen + 1,
              "buffer deviceCode phải dư chỗ cho ký tự kết thúc chuỗi");

namespace {
constexpr const char* kKeyApiKey     = "apikey";
constexpr const char* kKeyDeviceCode = "devcode";
constexpr const char* kKeyUnpaired   = "unpaired";

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

void clearUnpairedMarkerIfComplete() {
  if (s_apiKey[0] != '\0' && s_deviceCode[0] != '\0') {
    storage::nvsPutUInt8(kKeyUnpaired, 0);
  }
}
}  // namespace

namespace {

/// <summary>
/// GH-749 — nạp một trường định danh từ NVS, chỉ nhận khi giá trị còn NGUYÊN VẸN.
/// </summary>
/// <remarks>
/// Buffer đọc phải đúng cỡ buffer đích: bản cũ đọc deviceCode bằng buffer 96 byte rồi
/// <c>copySafe</c> xuống 64 byte, nên một giá trị quá khổ (do firmware trước GH-749 ghi vào)
/// sẽ âm thầm biến thành mã cụt sau mỗi lần khởi động. Ở đây, giá trị không vừa hoặc sai ký tự
/// bị BỎ, và ta quay về mặc định compile-time kèm log to — chạy bằng một định danh cụt thì
/// vừa không xác thực được vừa không ai biết vì sao.
/// </remarks>
bool loadValidated(const char* nvsKey, char* dst, size_t dstLen,
                   const char* fallback, const char* label) {
  if (!storage::nvsGetString(nvsKey, dst, dstLen)) {
    // Phân biệt "chưa cấu hình bao giờ" với "có giá trị nhưng đọc không nổi". Trường hợp sau
    // gần như chắc chắn là giá trị quá khổ do firmware TRƯỚC GH-749 ghi vào (`getString` trả 0
    // khi chuỗi không vừa buffer). Im lặng quay về mặc định ở đây thì người vận hành sẽ thấy
    // thiết bị tự đổi định danh mà không hiểu vì sao — nên phải nói ra.
    if (storage::nvsHasKey(nvsKey)) {
      Serial.printf("[identity] BỎ %s trong NVS — không đọc được (nhiều khả năng quá dài, "
                    "tối đa %u ký tự); dùng mặc định compile-time. Đặt lại bằng lệnh `set`.\n",
                    label, static_cast<unsigned>(dstLen - 1));
    }
    copySafe(dst, dstLen, fallback);
    return false;
  }

  const auto err = core::validateIdentityField(dst, dstLen);
  if (err != core::IdentityFieldError::Ok) {
    Serial.printf("[identity] BỎ %s trong NVS — %s; dùng mặc định compile-time\n",
                  label, core::describeIdentityError(err));
    copySafe(dst, dstLen, fallback);
    return false;
  }
  return true;
}

}  // namespace

void identityBegin() {
  if (storage::nvsGetUInt8(kKeyUnpaired, 0) == 1) {
    s_apiKey[0] = '\0';
    s_deviceCode[0] = '\0';
    s_apiKeyFromNvs = true;
    s_deviceCodeFromNvs = true;
    Serial.println("[identity] device is unpaired; waiting for a new setup QR");
    return;
  }

  // 1. Try NVS — mỗi khoá đọc bằng buffer ĐÚNG cỡ đích của nó (xem loadValidated).
  s_apiKeyFromNvs =
      loadValidated(kKeyApiKey, s_apiKey, sizeof(s_apiKey), API_KEY, "apiKey");
  s_deviceCodeFromNvs =
      loadValidated(kKeyDeviceCode, s_deviceCode, sizeof(s_deviceCode), DEVICE_CODE, "deviceCode");

  Serial.printf("[identity] deviceCode=%s (source=%s)\n",
                s_deviceCode, s_deviceCodeFromNvs ? "NVS" : "compile-time");
  char masked[24];
  maskedApiKey(masked, sizeof(masked));
  Serial.printf("[identity] apiKey=%s (source=%s)\n",
                masked, s_apiKeyFromNvs ? "NVS" : "compile-time");
}

const char* apiKey()     { return s_apiKey;     }
const char* deviceCode() { return s_deviceCode; }

// GH-749 — TỪ CHỐI trước khi ghi, thay vì ghi nguyên rồi cắt trong RAM.
//
// Bản cũ in chữ "truncate" nhưng KHÔNG cắt gì cả: `nvsPutString` nhận nguyên chuỗi dài, chỉ
// bản sao RAM bị cắt, rồi vẫn trả `true`. Người dùng thấy "thành công", thiết bị chạy giá trị
// A, còn NVS giữ giá trị B — khởi động lại là đổi định danh. Cắt bớt một khoá API cũng chẳng
// còn nghĩa lý gì: nó không xác thực được nữa. Vậy nên từ chối là lựa chọn duy nhất trung thực.
bool setApiKey(const char* newKey) {
  const auto err = core::validateIdentityField(newKey, sizeof(s_apiKey));
  if (err != core::IdentityFieldError::Ok) {
    Serial.printf("[identity] TỪ CHỐI apiKey — %s (tối đa %u ký tự)\n",
                  core::describeIdentityError(err),
                  static_cast<unsigned>(sizeof(s_apiKey) - 1));
    return false;
  }
  if (!storage::nvsPutString(kKeyApiKey, newKey)) return false;
  copySafe(s_apiKey, sizeof(s_apiKey), newKey);
  s_apiKeyFromNvs = true;
  clearUnpairedMarkerIfComplete();
  Serial.println("[identity] apiKey updated (NVS)");
  return true;
}

bool setDeviceCode(const char* newCode) {
  const auto err = core::validateIdentityField(newCode, sizeof(s_deviceCode));
  if (err != core::IdentityFieldError::Ok) {
    Serial.printf("[identity] TỪ CHỐI deviceCode — %s (tối đa %u ký tự)\n",
                  core::describeIdentityError(err),
                  static_cast<unsigned>(sizeof(s_deviceCode) - 1));
    return false;
  }
  if (!storage::nvsPutString(kKeyDeviceCode, newCode)) return false;
  copySafe(s_deviceCode, sizeof(s_deviceCode), newCode);
  s_deviceCodeFromNvs = true;
  clearUnpairedMarkerIfComplete();
  Serial.printf("[identity] deviceCode updated → %s\n", s_deviceCode);
  return true;
}

bool prepareForPairing() {
  if (!storage::nvsPutUInt8(kKeyUnpaired, 1)) return false;
  s_apiKey[0] = '\0';
  s_deviceCode[0] = '\0';
  s_apiKeyFromNvs = true;
  s_deviceCodeFromNvs = true;
  Serial.println("[identity] cleared; next boot will require a new setup QR");
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
