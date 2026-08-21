// ==================================================================
// Sprint 4 — S4-FW-01/02/03/04/05/06: MQTT client implementation
// ==================================================================
#include "net/mqtt_client.h"

#if __has_include("config.h")
  #include "config.h"
#else
  #include "config.example.h"
#endif

#include "config/device_identity.h"
#include "config/mqtt_config.h"
#include "core/net_config_rules.h"
#include "net/time_sync.h"
#include "net/ca_cert_embedded.h"
#include "net/host_resolver.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

#include <cctype>
#include <cstring>

namespace net {

namespace {

// ---- Static state ----
#if MQTT_USE_TLS
WiFiClientSecure s_wifiClient;
#else
WiFiClient       s_wifiClient;
#endif

PubSubClient    s_mqtt(s_wifiClient);
CommandCallback s_userCmdCb     = nullptr;
bool            s_inited        = false;
uint32_t        s_lastReconnectMs = 0;

// Stats
uint32_t s_pubOk          = 0;
uint32_t s_pubFail        = 0;
uint32_t s_connectCount   = 0;
uint32_t s_consecutiveFail = 0;

// IOT3-44 — streak lỗi XÁC THỰC (state 4/5), đếm tách khỏi s_consecutiveFail (vốn là lỗi publish).
uint32_t s_authFail = 0;
constexpr int kAuthFailThreshold = 5;

// CA cert buffer — load 1 lần lúc begin, giữ static để TLS handshake dùng lại.
// Heap để tránh stack overflow (CA PEM ~1.5KB).
String s_caCert;

// Topic buffer — IOT3-39: nay dựng từ `mqttcfg::topicPrefix()` (tối đa
// core::kMaxMqttPrefixChars = 96) + "/" + batterySerial + "/telemetry".
// Trường hợp dài nhất: 96 + 1 + 64 + 10 + 1 = 172 ⇒ lấy 192.
// Mỗi publish helper có buffer static riêng để tránh race (callback có thể publish
// trong khi caller đang publish).
constexpr size_t kTopicBufLen = 192;

/// Dựng `"<prefix>/<suffix>"` vào buffer. Tách riêng để 6 chỗ dựng topic chỉ còn MỘT công thức.
void buildTopic(char* buf, size_t bufLen, const char* suffix) {
  snprintf(buf, bufLen, "%s/%s", mqttcfg::topicPrefix(), suffix);
}

// ---- Ảnh chụp cấu hình mà phiên hiện tại được dựng theo (IOT3-41) ----
//
// Cần vì `mqttApplyConfig()` được gọi từ NHIỀU nơi: sau provision, ở nhánh đã-provisioned mỗi
// vòng loop đầu, và từ CLI. Không so trước thì mỗi lần gọi lại ngắt một phiên đang chạy tốt để
// dựng lại y nguyên — tự tạo khoảng mất kết nối, và mỗi khoảng ấy là số đo rơi mất.
char s_appliedHost  [mqttcfg::kHostBufLen];
char s_appliedUser  [mqttcfg::kUserBufLen];
char s_appliedPass  [mqttcfg::kPassBufLen];
char s_appliedPrefix[mqttcfg::kPrefixBufLen];
int  s_appliedPort = 0;

void snapshotAppliedConfig() {
  snprintf(s_appliedHost,   sizeof(s_appliedHost),   "%s", mqttcfg::host());
  snprintf(s_appliedUser,   sizeof(s_appliedUser),   "%s", mqttcfg::username());
  snprintf(s_appliedPass,   sizeof(s_appliedPass),   "%s", mqttcfg::password());
  snprintf(s_appliedPrefix, sizeof(s_appliedPrefix), "%s", mqttcfg::topicPrefix());
  s_appliedPort = mqttcfg::port();
}

bool configDiffersFromApplied() {
  return strcmp(s_appliedHost,   mqttcfg::host())        != 0 ||
         strcmp(s_appliedUser,   mqttcfg::username())    != 0 ||
         strcmp(s_appliedPass,   mqttcfg::password())    != 0 ||
         strcmp(s_appliedPrefix, mqttcfg::topicPrefix()) != 0 ||
         s_appliedPort != mqttcfg::port();
}

void onMessage(char* topic, byte* payload, unsigned int length) {
  // Sprint 4 chỉ handle 1 topic = solar/{dev}/cmd (subscribe duy nhất).
  // command_handler sẽ parse JSON + publish ack lên cmd/ack.
  Serial.printf("[mqtt] RX topic=%s len=%u\n", topic, length);
  if (s_userCmdCb && length > 0) {
    // Null-terminate cho callback xài như C-string. Bound bằng MQTT_MAX_PACKET_SIZE.
    static char rxBuf[MQTT_MAX_PACKET_SIZE];
    size_t copyLen = length < (sizeof(rxBuf) - 1) ? length : (sizeof(rxBuf) - 1);
    memcpy(rxBuf, payload, copyLen);
    rxBuf[copyLen] = '\0';
    s_userCmdCb(rxBuf, copyLen);
  }
}

bool loadCaCert() {
#if MQTT_USE_TLS
  // Ưu tiên CA nhúng trong firmware. LittleFS không dùng được cho việc này:
  // local_queue và chính file này đều gọi LittleFS.begin(true) tức
  // format-nếu-mount-lỗi, mà ảnh mklittlefs không mount được nên phân vùng
  // bị xoá sạch mỗi lần boot, cuốn theo ca_cert.pem.
  if (kMqttCaCert[0] != '\0') {
    // KHÔNG trim(): mbedTLS đòi PEM kết thúc bằng ký tự xuống dòng sau dòng
    // -----END CERTIFICATE-----. Cắt mất nó là bắt tay hỏng với lỗi -9984
    // "X509 Certificate verification failed", rất dễ tưởng nhầm sai CA.
    s_caCert = String(kMqttCaCert);
    if (s_caCert.indexOf("-----BEGIN CERTIFICATE-----") >= 0) {
      s_wifiClient.setCACert(s_caCert.c_str());
      Serial.printf("[mqtt] CA cert nhúng trong firmware (%u bytes)\n",
                    static_cast<unsigned>(s_caCert.length()));
      return true;
    }
    Serial.println("[mqtt] CA nhúng sai format — thử đọc từ LittleFS");
  }

  // Never auto-format here: the same LittleFS partition may contain offline
  // telemetry. A missing CA must disable MQTT, not erase unsent readings.
  if (!LittleFS.begin(false /*formatOnFail*/)) {
    Serial.println("[mqtt] LittleFS mount FAIL — không load được CA cert");
    return false;
  }
  if (!LittleFS.exists(MQTT_CA_CERT_PATH)) {
    Serial.printf("[mqtt] CA cert KHÔNG tồn tại tại %s\n", MQTT_CA_CERT_PATH);
    Serial.println("[mqtt]   → chạy `infra/mqtt/scripts/gen-certs.sh`");
    Serial.println("[mqtt]   → copy ca.crt vào firmware-esp32/data/ca_cert.pem");
    Serial.println("[mqtt]   → pio run -t uploadfs");
    return false;
  }
  File f = LittleFS.open(MQTT_CA_CERT_PATH, "r");
  if (!f) return false;
  s_caCert = f.readString();
  f.close();
  if (s_caCert.length() < 100) {
    Serial.printf("[mqtt] CA cert quá ngắn (%u bytes) — file hỏng?\n",
                  static_cast<unsigned>(s_caCert.length()));
    return false;
  }
  // Sanity check PEM marker — bắt nhầm format (DER binary, JSON, hoặc file rác).
  // PEM cert PHẢI bắt đầu bằng "-----BEGIN CERTIFICATE-----".
  if (s_caCert.indexOf("-----BEGIN CERTIFICATE-----") < 0) {
    Serial.printf("[mqtt] CA cert KHÔNG phải PEM format (thiếu -----BEGIN CERTIFICATE-----).\n");
    Serial.printf("[mqtt]   Đầu file: '%s'\n", s_caCert.substring(0, 40).c_str());
    Serial.println("[mqtt]   → Sửa: cp infra/mqtt/mosquitto/certs/ca.crt data/ca_cert.pem (PEM ASCII)");
    return false;
  }
  s_wifiClient.setCACert(s_caCert.c_str());
  Serial.printf("[mqtt] CA cert loaded (%u bytes) từ %s\n",
                static_cast<unsigned>(s_caCert.length()), MQTT_CA_CERT_PATH);
  return true;
#else
  Serial.println("[mqtt] MQTT_USE_TLS=0 — plain MQTT (KHÔNG dùng cho production)");
  return true;
#endif
}

void configureBrokerEndpoint() {
  IPAddress resolved;
  if (core::isMdnsHostname(mqttcfg::host()) &&
      resolveMdnsHost(mqttcfg::host(), resolved)) {
    s_mqtt.setServer(resolved, mqttcfg::port());
    return;
  }
  s_mqtt.setServer(mqttcfg::host(), mqttcfg::port());
}

bool tryConnect() {
  if (!WiFi.isConnected()) return false;

  // Re-resolve ở mỗi chu kỳ reconnect (resolver có cache). Nếu laptop/backend
  // nhận IP DHCP mới mà ESP vẫn online, MQTT tự tìm lại sau tối đa một TTL.
  configureBrokerEndpoint();

  // (Re)config LWT mỗi lần connect — vì deviceCode có thể đổi qua Serial CLI
  // (S2-FW-01 hot reload). LWT topic = solar/{dev}/status, payload="offline",
  // QoS 1, retain=true (S4-FW-02 spec).
  static char willTopicBuf[kTopicBufLen];
  buildTopic(willTopicBuf, kTopicBufLen, "status");   // IOT3-39

  Serial.printf("[mqtt] connect host=%s port=%d user=%s lwt=%s ...\n",
                mqttcfg::host(), mqttcfg::port(),
                mqttcfg::username(), willTopicBuf);

  // IOT3-38 — clientId lấy deviceCode RUNTIME, không lấy macro `MQTT_CLIENT_ID` (vốn nở ra
  // `DEVICE_CODE` compile-time). Đổi mã thiết bị qua CLI/NVS rồi mà clientId vẫn là mã cũ thì
  // broker thấy hai phiên cùng clientId sẽ đá phiên trước ra — hai thiết bị đạp nhau vô tận.
  //
  // PubSubClient::connect(clientId, user, pass, willTopic, willQos, willRetain, willMsg)
  bool ok = s_mqtt.connect(
      identity::deviceCode(),
      mqttcfg::username(),
      mqttcfg::password(),
      willTopicBuf,           // S4-FW-02 will topic
      1,                       // willQos
      true,                    // willRetain
      "offline");              // willMessage

  if (!ok) {
    int state = s_mqtt.state();
    // PubSubClient state codes — map sang lý do dễ debug.
    const char* reason;
    switch (state) {
      case -4: reason = "MQTT_CONNECTION_TIMEOUT (broker không reply)"; break;
      case -3: reason = "MQTT_CONNECTION_LOST (socket drop giữa chừng)"; break;
      case -2: reason = "MQTT_CONNECT_FAILED (TCP/TLS connect lỗi — verify host/port/CA cert/NTP)"; break;
      case -1: reason = "MQTT_DISCONNECTED"; break;
      case  1: reason = "MQTT_CONNECT_BAD_PROTOCOL (broker từ chối phiên bản MQTT)"; break;
      case  2: reason = "MQTT_CONNECT_BAD_CLIENT_ID (clientId trùng hoặc invalid)"; break;
      case  3: reason = "MQTT_CONNECT_UNAVAILABLE (broker chưa sẵn sàng)"; break;
      case  4: reason = "MQTT_CONNECT_BAD_CREDENTIALS (sai user/password — chạy add-device.sh)"; break;
      case  5: reason = "MQTT_CONNECT_UNAUTHORIZED (auth OK nhưng ACL từ chối — check acl.conf)"; break;
      default: reason = "(unknown state)"; break;
    }
    Serial.printf("[mqtt] connect FAIL state=%d — %s\n", state, reason);

    // IOT3-44 — đếm RIÊNG lỗi xác thực. Phải tách khỏi lỗi mạng (-2/-3/-4): mất mạng thì chờ
    // là xong, còn sai thông tin đăng nhập thì chờ bao lâu cũng vô ích — chỉ `/provision` lấy
    // credential mới mới cứu được. Gộp chung sẽ hoặc là re-provision loạn mỗi lần rớt WiFi,
    // hoặc là không bao giờ re-provision.
    if (state == 4 || state == 5) {
      s_authFail++;
      Serial.printf("[mqtt]   ↑ lỗi XÁC THỰC lần thứ %lu liên tiếp (ngưỡng %d → xin credential mới)\n",
                    static_cast<unsigned long>(s_authFail), kAuthFailThreshold);
    } else {
      // Lỗi mạng KHÔNG được xoá streak xác thực — mạng chập chờn xen giữa các lần bị từ chối
      // sẽ reset đếm về 0 mãi mãi, và thiết bị không bao giờ tự lành.
    }
    return false;
  }

  s_connectCount++;
  s_consecutiveFail = 0;
  s_authFail = 0;   // IOT3-44 — nối được nghĩa là credential đang dùng ĐÚNG
  Serial.printf("[mqtt] CONNECTED (count=%lu)\n",
                static_cast<unsigned long>(s_connectCount));

  // S4-FW-03: publish "online" retain QoS 1 lên status — override LWT "offline".
  bool pubOk = s_mqtt.publish(willTopicBuf, "online", true /*retain*/);
  Serial.printf("[mqtt] status=online retain → %s\n", pubOk ? "OK" : "FAIL");

  // S4-FW-03: subscribe downlink cmd.
  static char cmdTopicBuf[kTopicBufLen];
  buildTopic(cmdTopicBuf, kTopicBufLen, "cmd");   // IOT3-39
  bool subOk = s_mqtt.subscribe(cmdTopicBuf, 1 /*QoS 1*/);
  Serial.printf("[mqtt] subscribe %s → %s\n",
                cmdTopicBuf, subOk ? "OK" : "FAIL");

  return true;
}

}  // namespace

// ====================================================================
// Public API
// ====================================================================

// IOT3-40 — kiểm chéo tiền tố topic đang dùng với tiền tố suy ra từ deviceCode.
//
// Backend cấp `mqttTopicPrefix` (= `MqttBrokerEndpointProvider.TopicPrefixFor()`), còn ACL của
// Mosquitto lại khớp theo `pattern write solar/%u/...` với `%u` = username. Ba nguồn — tiền tố
// backend cấp, username, và deviceCode — phải quy về CÙNG một chuỗi chữ thường. Lệch một chữ hoa
// là mất cả uplink lẫn downlink mà **không bên nào báo lỗi**: broker chỉ lặng lẽ không chuyển tin.
//
// So sánh này chỉ CẢNH BÁO, không chặn — có thể backend cố ý dùng tiền tố khác (đổi quy ước), và
// chặn kết nối vì một cảnh báo suy đoán còn tệ hơn.
void warnIfTopicPrefixMismatch() {
  const char* actual = mqttcfg::topicPrefix();

  char expected[mqttcfg::kPrefixBufLen];
  if (core::deriveTopicPrefix(identity::deviceCode(), expected, sizeof(expected)) == 0) {
    Serial.println("[mqtt] ⚠ không suy được tiền tố từ deviceCode để đối chiếu (mã rỗng?)");
    return;
  }

  if (strcmp(actual, expected) != 0) {
    Serial.printf("[mqtt] ⚠ tiền tố topic đang dùng '%s' KHÁC tiền tố suy từ deviceCode '%s'.\n",
                  actual, expected);
    Serial.println("[mqtt]    Nếu không cố ý: đây chính là kiểu lỗi làm broker im lặng nuốt tin.");
    Serial.println("[mqtt]    → so lại MqttTopicPrefix backend trả về với mã thiết bị đang nạp.");
  }

  // Username phải trùng đúng phần sau "solar/" của tiền tố — đó là thứ ACL dùng làm `%u`.
  const char* seg = strchr(actual, '/');
  const char* user = mqttcfg::username();
  if (seg != nullptr && user != nullptr && strcmp(seg + 1, user) != 0) {
    Serial.printf("[mqtt] ⚠ username MQTT '%s' KHÁC đoạn thiết bị trong topic '%s' — "
                  "ACL `pattern ... solar/%%u/...` sẽ TỪ CHỐI publish (state=5).\n",
                  user, seg + 1);
  }
}

bool mqttBegin() {
  if (s_inited) return true;

  // IOT3-40 — chưa provision thì KHÔNG khởi tạo, và chỉ nói MỘT lần.
  // Trước IoT-3, broker/credential là macro compile-time nên luôn "có sẵn" (dù là placeholder),
  // firmware cứ thế quay vòng connect-fail mỗi 5 giây. Nay giá trị đến lúc chạy, nên trạng thái
  // "chưa có" là hợp lệ và bình thường — không phải lỗi để mà gào lên.
  if (!mqttcfg::isConfigured()) {
    static bool s_warned = false;
    if (!s_warned) {
      s_warned = true;
      Serial.println("[mqtt] chưa có cấu hình MQTT (chưa provision) — bỏ qua, chạy HTTPS-only.");
      Serial.println("[mqtt]   Provision xong sẽ tự khởi tạo qua net::mqttApplyConfig().");
    }
    return false;
  }

  if (!loadCaCert()) {
#if MQTT_USE_TLS
    Serial.println("[mqtt] mqttBegin FAIL — CA cert chưa sẵn sàng");
    return false;
#endif
  }

  configureBrokerEndpoint();
  s_mqtt.setBufferSize(MQTT_MAX_PACKET_SIZE);
  s_mqtt.setKeepAlive(MQTT_KEEPALIVE_SEC);
  s_mqtt.setCallback(onMessage);

  warnIfTopicPrefixMismatch();

  s_inited = true;
  snapshotAppliedConfig();
  Serial.printf("[mqtt] init OK — broker=%s:%d tls=%d prefix=%s buf=%u keepalive=%ds\n",
                mqttcfg::host(), mqttcfg::port(), MQTT_USE_TLS, mqttcfg::topicPrefix(),
                static_cast<unsigned>(MQTT_MAX_PACKET_SIZE),
                static_cast<int>(MQTT_KEEPALIVE_SEC));
  return true;
}

// IOT3-41
bool mqttApplyConfig() {
  // Chưa init (chưa provision lúc boot) ⇒ đây là lần đầu có cấu hình: khởi tạo luôn.
  if (!s_inited) return mqttBegin();

  if (!mqttcfg::isConfigured()) {
    Serial.println("[mqtt] cấu hình mới KHÔNG dùng được — giữ nguyên phiên đang chạy");
    return false;
  }

  // IOT3-46 — hàm này được gọi cả ở nhánh "đã provision từ trước", tức mỗi lần boot. Cấu hình
  // y hệt thì KHÔNG đụng vào phiên đang chạy.
  if (!configDiffersFromApplied()) {
    return true;
  }

  configureBrokerEndpoint();
  if (s_mqtt.connected()) {
    Serial.println("[mqtt] cấu hình đổi → ngắt kết nối để dựng lại LWT/topic/subscribe");
    s_mqtt.disconnect();
  }
  // Nối lại NGAY, không đợi hết MQTT_RECONNECT_INTERVAL_MS — cùng lý do với mqttOnIdentityChanged().
  s_lastReconnectMs = 0;
  // Credential mới ⇒ streak xác thực cũ không còn ý nghĩa, xoá kẻo re-provision oan.
  s_authFail = 0;

  snapshotAppliedConfig();
  warnIfTopicPrefixMismatch();
  Serial.printf("[mqtt] áp cấu hình mới — broker=%s:%d prefix=%s\n",
                mqttcfg::host(), mqttcfg::port(), mqttcfg::topicPrefix());
  return true;
}

void mqttOnIdentityChanged() {
  if (!s_inited) return;
  if (s_mqtt.connected()) {
    Serial.println("[mqtt] deviceCode đổi → ngắt kết nối để dựng lại LWT/topic/subscribe");
    s_mqtt.disconnect();
  }
  // Cho phép kết nối lại NGAY, không phải đợi hết MQTT_RECONNECT_INTERVAL_MS: thiết bị đang
  // câm downlink cho tới khi subscribe lại được.
  s_lastReconnectMs = 0;
}

void mqttTick() {
  if (!s_inited) return;
  if (!WiFi.isConnected()) return;

#if MQTT_USE_TLS
  // TLS cert validation cần NTP synced — broker cert có notBefore=2026 GMT,
  // ESP32 boot time=1970 sẽ fail "certificate not yet valid" và loop spam log
  // tới khi NTP sync. Gate tryConnect bằng net::timeIsSynced() — Sprint 1 đã có
  // setup NTP qua time_sync.cpp.
  if (!net::timeIsSynced()) {
    static uint32_t s_lastNtpWaitLogMs = 0;
    uint32_t now = millis();
    if (now - s_lastNtpWaitLogMs > 10000) {
      s_lastNtpWaitLogMs = now;
      Serial.println("[mqtt] đợi NTP sync trước khi TLS connect (tránh cert 'not yet valid')...");
    }
    return;
  }
#endif

  if (!s_mqtt.connected()) {
    uint32_t now = millis();
    if (now - s_lastReconnectMs < MQTT_RECONNECT_INTERVAL_MS) return;
    s_lastReconnectMs = now;
    tryConnect();
    return;
  }

  s_mqtt.loop();
}

bool mqttIsConnected() {
  return s_mqtt.connected();
}

void mqttSetCommandCallback(CommandCallback cb) {
  s_userCmdCb = cb;
}

// ---- Publish helpers ----

namespace {
// GH-746 — ĐÃ BỎ tham số `qos`: PubSubClient v2.8 không có overload QoS, nên trước đây tham
// số này bị vứt ngay bằng một dòng cast-to-void — một tham số giả khiến call site tưởng mình
// đang chọn QoS 1. (Test test_mqtt_qos_contract chặn dòng cast đó quay lại; vì guard so chuỗi
// thô nên comment ở đây cố ý KHÔNG viết lại nguyên văn token đó.)
bool publishWithStats(const char* topic, const char* payload, size_t len, bool retain) {
  if (!s_mqtt.connected()) {
    s_pubFail++;
    s_consecutiveFail++;
    return false;
  }
  // PubSubClient::publish overload duy nhất: (topic, payload, length, retain) → QoS 0.
  // `ok` = "đã đẩy được vào socket TCP", KHÔNG phải "broker đã nhận" (xem kPublishGuarantee).
  bool ok = s_mqtt.publish(topic,
                           reinterpret_cast<const uint8_t*>(payload),
                           len, retain);
  if (ok) {
    s_pubOk++;
    s_consecutiveFail = 0;
  } else {
    s_pubFail++;
    s_consecutiveFail++;
  }
  return ok;
}
}  // namespace

bool mqttPublishTelemetry(const char* batterySerial,
                          const char* payload, size_t payloadLen) {
  // Defensive: batterySerial rỗng tạo topic invalid `solar/dev//telemetry` —
  // broker ACL `solar/%u/+/telemetry` reject. Báo lỗi early để không tốn round-trip.
  if (batterySerial == nullptr || batterySerial[0] == '\0') {
    Serial.println("[mqtt] publishTelemetry FAIL — batterySerial rỗng");
    s_pubFail++;
    s_consecutiveFail++;
    return false;
  }
  static char topicBuf[kTopicBufLen];
  snprintf(topicBuf, kTopicBufLen, "%s/%s/telemetry",     // IOT3-39
           mqttcfg::topicPrefix(), batterySerial);
  return publishWithStats(topicBuf, payload, payloadLen, false);
}

bool mqttPublishHeartbeat(const char* payload, size_t payloadLen) {
  static char topicBuf[kTopicBufLen];
  buildTopic(topicBuf, kTopicBufLen, "heartbeat");   // IOT3-39
  return publishWithStats(topicBuf, payload, payloadLen, false);
}

bool mqttPublishStatus(const char* payload, size_t payloadLen) {
  static char topicBuf[kTopicBufLen];
  buildTopic(topicBuf, kTopicBufLen, "status");   // IOT3-39
  return publishWithStats(topicBuf, payload, payloadLen, true);
}

bool mqttPublishCmdAck(const char* payload, size_t payloadLen) {
  static char topicBuf[kTopicBufLen];
  buildTopic(topicBuf, kTopicBufLen, "cmd/ack");   // IOT3-39
  return publishWithStats(topicBuf, payload, payloadLen, false);
}

// ---- Stats ----
uint32_t mqttPublishOkCount()         { return s_pubOk; }
uint32_t mqttPublishFailCount()       { return s_pubFail; }
uint32_t mqttConnectCount()           { return s_connectCount; }
uint32_t mqttConsecutiveFailCount()   { return s_consecutiveFail; }
void     mqttResetConsecutiveFails()  { s_consecutiveFail = 0; }

// IOT3-44
uint32_t mqttAuthFailureCount()       { return s_authFail; }
int      mqttAuthFailureThreshold()   { return kAuthFailThreshold; }
void     mqttResetAuthFailures()      { s_authFail = 0; }

}  // namespace net
