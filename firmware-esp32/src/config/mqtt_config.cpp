// ==================================================================
// IOT3-37 — hiện thực MQTT runtime store. Xem mqtt_config.h.
// ==================================================================
#include "config/mqtt_config.h"

#include <Arduino.h>
#include <string.h>

#include "config/device_identity.h"
#include "core/identity_validation.h"
#include "config/nvs_store.h"

#if __has_include("config.h")
  #include "config.h"
#else
  #include "config.example.h"
#endif

namespace mqttcfg {

namespace {

// ⚠️ Khoá NVS tối đa 15 ký tự (Preferences spec).
constexpr const char* kKeyHost   = "mqhost";
constexpr const char* kKeyPort   = "mqport";
constexpr const char* kKeyTls    = "mqtls";
constexpr const char* kKeyUser   = "mquser";
constexpr const char* kKeyPass   = "mqpass";
constexpr const char* kKeyPrefix = "mqprefix";

char s_host  [kHostBufLen];
char s_user  [kUserBufLen];
char s_pass  [kPassBufLen];
char s_prefix[kPrefixBufLen];
int  s_port    = 0;
bool s_wantTls = false;
bool s_fromNvs = false;
bool s_inited  = false;

void copySafe(char* dst, size_t dstLen, const char* src) {
  if (dst == nullptr || dstLen == 0) return;
  if (src == nullptr) { dst[0] = '\0'; return; }
  strncpy(dst, src, dstLen - 1);
  dst[dstLen - 1] = '\0';
}

/// Suy tiền tố từ deviceCode hiện hành và ghi vào s_prefix.
void deriveAndStorePrefix() {
  if (core::deriveTopicPrefix(identity::deviceCode(), s_prefix, sizeof(s_prefix)) == 0) {
    // deviceCode rỗng/quá dài — không dựng nổi tiền tố. Để rỗng và để mqttBegin() từ chối nối,
    // tốt hơn là publish lên một topic sai rồi bị ACL chặn im lặng.
    s_prefix[0] = '\0';
  }
}

/// Đọc chuỗi từ NVS, chỉ nhận khi hợp lệ. Trả false ⇒ caller giữ giá trị fallback.
bool loadString(const char* key, char* dst, size_t dstLen, size_t maxChars, const char* label) {
  if (!storage::nvsGetString(key, dst, dstLen)) {
    if (storage::nvsHasKey(key)) {
      Serial.printf("[mqttcfg] BỎ %s trong NVS — không đọc được (quá dài? tối đa %u ký tự)\n",
                    label, static_cast<unsigned>(maxChars));
    }
    return false;
  }
  if (dst[0] == '\0') return false;
  // Dùng luật của apiKey/deviceCode: host/user/pass/prefix đều KHÔNG được chứa khoảng trắng
  // (chúng đi vào topic MQTT và gói CONNECT), khác hẳn SSID WiFi.
  const auto err = core::validateIdentityField(dst, dstLen);
  if (err != core::IdentityFieldError::Ok) {
    Serial.printf("[mqttcfg] BỎ %s trong NVS — %s\n", label, core::describeIdentityError(err));
    dst[0] = '\0';
    return false;
  }
  return true;
}

}  // namespace

void begin() {
  // 1) Fallback compile-time trước, rồi NVS ghi đè từng trường.
  copySafe(s_host, sizeof(s_host), MQTT_BROKER_HOST);
  copySafe(s_user, sizeof(s_user), MQTT_USERNAME);
  copySafe(s_pass, sizeof(s_pass), MQTT_PASSWORD);
  s_port    = MQTT_BROKER_PORT;
  s_wantTls = (MQTT_USE_TLS != 0);
  deriveAndStorePrefix();

  bool any = false;
  char buf[kPrefixBufLen];

  if (loadString(kKeyHost, buf, kHostBufLen, core::kMaxMqttHostChars, "host")) {
    copySafe(s_host, sizeof(s_host), buf); any = true;
  }
  if (loadString(kKeyUser, buf, kUserBufLen, core::kMaxMqttUserChars, "username")) {
    copySafe(s_user, sizeof(s_user), buf); any = true;
  }
  if (loadString(kKeyPass, buf, kPassBufLen, core::kMaxMqttPassChars, "mật khẩu")) {
    copySafe(s_pass, sizeof(s_pass), buf); any = true;
  }
  if (loadString(kKeyPrefix, buf, kPrefixBufLen, core::kMaxMqttPrefixChars, "topic prefix")) {
    copySafe(s_prefix, sizeof(s_prefix), buf); any = true;
  }

  const int32_t storedPort = storage::nvsGetInt32(kKeyPort, 0);
  if (core::mqttPortUsable(static_cast<int>(storedPort))) {
    s_port = static_cast<int>(storedPort); any = true;
  } else if (storedPort != 0) {
    Serial.printf("[mqttcfg] BỎ port=%ld trong NVS — ngoài dải [1,65535]\n",
                  static_cast<long>(storedPort));
  }

  if (storage::nvsHasKey(kKeyTls)) {
    s_wantTls = storage::nvsGetUInt8(kKeyTls, s_wantTls ? 1 : 0) == 1;
    any = true;
  }

  s_fromNvs = any;
  s_inited  = true;

  Serial.printf("[mqttcfg] host=%s port=%d prefix=%s user=%s (nguồn=%s) usable=%s\n",
                s_host, s_port, s_prefix, s_user,
                s_fromNvs ? "NVS" : "compile-time",
                isConfigured() ? "có" : "KHÔNG");

  // Quyết định Q2: TLS là compile-time. Nếu backend bảo khác thì phải nói to — nối sai lớp
  // vận chuyển sẽ thất bại ở tầng TCP với thông báo rất khó truy.
  const bool buildTls = (MQTT_USE_TLS != 0);
  if (s_wantTls != buildTls) {
    Serial.printf("[mqttcfg] ⚠ LỆCH TLS — backend muốn %s nhưng firmware build với MQTT_USE_TLS=%d.\n",
                  s_wantTls ? "TLS" : "plain", MQTT_USE_TLS);
    Serial.println("[mqttcfg]   Đường TLS do compile-time quyết (Q2). Phải build lại firmware "
                   "cho khớp, hoặc sửa Mqtt__UseTls ở backend.");
  }
}

const char* host()          { return s_host; }
int         port()          { return s_port; }
bool        brokerWantsTls(){ return s_wantTls; }
const char* username()      { return s_user; }
const char* password()      { return s_pass; }
bool        isFromNvs()     { return s_fromNvs; }

const char* topicPrefix() {
  // Gọi trước begin() vẫn phải ra được thứ dùng tạm — mqtt_client có thể dựng LWT rất sớm.
  if (!s_inited && s_prefix[0] == '\0') deriveAndStorePrefix();
  if (s_prefix[0] == '\0') deriveAndStorePrefix();
  return s_prefix;
}

bool isConfigured() {
  return core::mqttConfigUsable(s_host, s_port, s_user, s_pass) && topicPrefix()[0] != '\0';
}

bool applyFromProvision(const char* newHost, int newPort, bool newUseTls,
                        const char* prefix, const char* user, const char* pass) {
  // --- Kiểm TOÀN BỘ trước khi ghi bất kỳ trường nào ---
  // Ghi nửa vời (host mới + mật khẩu cũ) tạo ra cấu hình không tồn tại ở đâu cả, và lần boot
  // sau nạp lên sẽ nối thất bại mà log không chỉ ra được sai ở đâu.
  struct Field { const char* v; size_t buf; const char* label; };
  const Field fields[] = {
      { newHost, kHostBufLen, "host" },
      { user,    kUserBufLen, "username" },
      { pass,    kPassBufLen, "mật khẩu" },
  };
  for (const auto& f : fields) {
    const auto err = core::validateIdentityField(f.v, f.buf);
    if (err != core::IdentityFieldError::Ok) {
      Serial.printf("[mqttcfg] TỪ CHỐI cấu hình provision — %s %s\n",
                    f.label, core::describeIdentityError(err));
      return false;
    }
  }
  if (!core::mqttPortUsable(newPort)) {
    Serial.printf("[mqttcfg] TỪ CHỐI cấu hình provision — port=%d ngoài dải [1,65535]\n", newPort);
    return false;
  }

  char wanted[kPrefixBufLen];
  if (prefix != nullptr && prefix[0] != '\0') {
    if (core::validateIdentityField(prefix, kPrefixBufLen) != core::IdentityFieldError::Ok) {
      Serial.println("[mqttcfg] TỪ CHỐI cấu hình provision — topic prefix không hợp lệ");
      return false;
    }
    copySafe(wanted, sizeof(wanted), prefix);
  } else if (core::deriveTopicPrefix(identity::deviceCode(), wanted, sizeof(wanted)) == 0) {
    Serial.println("[mqttcfg] TỪ CHỐI cấu hình provision — không suy được topic prefix từ deviceCode");
    return false;
  }

  // --- Có thay đổi thật không? ---
  const bool changed =
      strcmp(s_host, newHost)  != 0 ||
      strcmp(s_user, user)     != 0 ||
      strcmp(s_pass, pass)     != 0 ||
      strcmp(s_prefix, wanted) != 0 ||
      s_port    != newPort ||
      s_wantTls != newUseTls;

  if (!changed) {
    Serial.println("[mqttcfg] cấu hình provision trùng với đang chạy — không ghi, không ngắt phiên");
    return false;
  }

  if (!storage::nvsPutString(kKeyHost,   newHost))            return false;
  if (!storage::nvsPutInt32 (kKeyPort,   newPort))            return false;
  if (!storage::nvsPutUInt8 (kKeyTls,    newUseTls ? 1 : 0))  return false;
  if (!storage::nvsPutString(kKeyUser,   user))               return false;
  if (!storage::nvsPutString(kKeyPass,   pass))               return false;
  if (!storage::nvsPutString(kKeyPrefix, wanted))             return false;

  copySafe(s_host,   sizeof(s_host),   newHost);
  copySafe(s_user,   sizeof(s_user),   user);
  copySafe(s_pass,   sizeof(s_pass),   pass);
  copySafe(s_prefix, sizeof(s_prefix), wanted);
  s_port    = newPort;
  s_wantTls = newUseTls;
  s_fromNvs = true;

  Serial.printf("[mqttcfg] áp cấu hình từ provision: host=%s port=%d tls=%d prefix=%s user=%s\n",
                s_host, s_port, s_wantTls ? 1 : 0, s_prefix, s_user);
  return true;
}

bool setBroker(const char* newHost, int newPort) {
  const auto err = core::validateIdentityField(newHost, kHostBufLen);
  if (err != core::IdentityFieldError::Ok) {
    Serial.printf("[mqttcfg] TỪ CHỐI host — %s\n", core::describeIdentityError(err));
    return false;
  }
  if (!core::mqttPortUsable(newPort)) {
    Serial.printf("[mqttcfg] TỪ CHỐI port=%d — ngoài dải [1,65535]\n", newPort);
    return false;
  }
  if (!storage::nvsPutString(kKeyHost, newHost)) return false;
  if (!storage::nvsPutInt32 (kKeyPort, newPort)) return false;
  copySafe(s_host, sizeof(s_host), newHost);
  s_port = newPort;
  s_fromNvs = true;
  Serial.printf("[mqttcfg] broker → %s:%d\n", s_host, s_port);
  return true;
}

bool setCredential(const char* user, const char* pass) {
  const auto userErr = core::validateIdentityField(user, kUserBufLen);
  if (userErr != core::IdentityFieldError::Ok) {
    Serial.printf("[mqttcfg] TỪ CHỐI username — %s\n", core::describeIdentityError(userErr));
    return false;
  }
  const auto passErr = core::validateIdentityField(pass, kPassBufLen);
  if (passErr != core::IdentityFieldError::Ok) {
    Serial.printf("[mqttcfg] TỪ CHỐI mật khẩu — %s\n", core::describeIdentityError(passErr));
    return false;
  }
  if (!storage::nvsPutString(kKeyUser, user)) return false;
  if (!storage::nvsPutString(kKeyPass, pass)) return false;
  copySafe(s_user, sizeof(s_user), user);
  copySafe(s_pass, sizeof(s_pass), pass);
  s_fromNvs = true;
  Serial.println("[mqttcfg] đã đổi thông tin đăng nhập MQTT");
  return true;
}

bool setTopicPrefix(const char* prefix) {
  const auto err = core::validateIdentityField(prefix, kPrefixBufLen);
  if (err != core::IdentityFieldError::Ok) {
    Serial.printf("[mqttcfg] TỪ CHỐI topic prefix — %s\n", core::describeIdentityError(err));
    return false;
  }
  if (!storage::nvsPutString(kKeyPrefix, prefix)) return false;
  copySafe(s_prefix, sizeof(s_prefix), prefix);
  s_fromNvs = true;
  Serial.printf("[mqttcfg] topic prefix → %s\n", s_prefix);
  return true;
}

bool clear() {
  bool ok = true;
  for (const char* k : { kKeyHost, kKeyUser, kKeyPass, kKeyPrefix })
    ok = storage::nvsPutString(k, "") && ok;
  ok = storage::nvsPutInt32(kKeyPort, 0) && ok;
  ok = storage::nvsPutUInt8(kKeyTls, MQTT_USE_TLS != 0 ? 1 : 0) && ok;

  copySafe(s_host, sizeof(s_host), MQTT_BROKER_HOST);
  copySafe(s_user, sizeof(s_user), MQTT_USERNAME);
  copySafe(s_pass, sizeof(s_pass), MQTT_PASSWORD);
  s_port    = MQTT_BROKER_PORT;
  s_wantTls = (MQTT_USE_TLS != 0);
  deriveAndStorePrefix();
  s_fromNvs = false;
  Serial.println("[mqttcfg] đã xoá cấu hình MQTT trong NVS — quay về compile-time");
  return ok;
}

void printStatus() {
  Serial.println("==== MQTT config ====");
  Serial.printf("  host      = %s:%d (%s)\n", s_host, s_port, s_fromNvs ? "NVS" : "compile-time");
  Serial.printf("  tls       = build:%d / backend muốn:%d\n", MQTT_USE_TLS, s_wantTls ? 1 : 0);
  Serial.printf("  username  = %s\n", s_user);
  Serial.printf("  mật khẩu  = %s\n", s_pass[0] == '\0' ? "(rỗng)" : "***");
  Serial.printf("  prefix    = %s\n", topicPrefix());
  Serial.printf("  usable    = %s\n", isConfigured() ? "có" : "KHÔNG");
  Serial.println("=====================");
}

}  // namespace mqttcfg
