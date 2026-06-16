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
#include "net/time_sync.h"

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

// CA cert buffer — load 1 lần lúc begin, giữ static để TLS handshake dùng lại.
// Heap để tránh stack overflow (CA PEM ~1.5KB).
String s_caCert;

// Topic buffer — deviceCode max 64 + batterySerial max ~32 + envelope segments
// ≈ 120 chars max. Buffer 160 dư an toàn. Mỗi publish helper có buffer static
// riêng để tránh race (callback có thể publish trong khi caller đang publish).
constexpr size_t kTopicBufLen = 160;

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
  if (!LittleFS.begin(true /*formatOnFail*/)) {
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

bool tryConnect() {
  if (!WiFi.isConnected()) return false;

  // (Re)config LWT mỗi lần connect — vì deviceCode có thể đổi qua Serial CLI
  // (S2-FW-01 hot reload). LWT topic = solar/{dev}/status, payload="offline",
  // QoS 1, retain=true (S4-FW-02 spec).
  static char willTopicBuf[kTopicBufLen];
  snprintf(willTopicBuf, kTopicBufLen, "%s/%s/status",
           MQTT_TOPIC_PREFIX, identity::deviceCode());

  Serial.printf("[mqtt] connect host=%s port=%d user=%s lwt=%s ...\n",
                MQTT_BROKER_HOST, MQTT_BROKER_PORT,
                MQTT_USERNAME, willTopicBuf);

  // PubSubClient::connect(clientId, user, pass, willTopic, willQos, willRetain, willMsg)
  bool ok = s_mqtt.connect(
      MQTT_CLIENT_ID,
      MQTT_USERNAME,
      MQTT_PASSWORD,
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
    return false;
  }

  s_connectCount++;
  s_consecutiveFail = 0;
  Serial.printf("[mqtt] CONNECTED (count=%lu)\n",
                static_cast<unsigned long>(s_connectCount));

  // S4-FW-03: publish "online" retain QoS 1 lên status — override LWT "offline".
  bool pubOk = s_mqtt.publish(willTopicBuf, "online", true /*retain*/);
  Serial.printf("[mqtt] status=online retain → %s\n", pubOk ? "OK" : "FAIL");

  // S4-FW-03: subscribe downlink cmd.
  static char cmdTopicBuf[kTopicBufLen];
  snprintf(cmdTopicBuf, kTopicBufLen, "%s/%s/cmd",
           MQTT_TOPIC_PREFIX, identity::deviceCode());
  bool subOk = s_mqtt.subscribe(cmdTopicBuf, 1 /*QoS 1*/);
  Serial.printf("[mqtt] subscribe %s → %s\n",
                cmdTopicBuf, subOk ? "OK" : "FAIL");

  return true;
}

}  // namespace

// ====================================================================
// Public API
// ====================================================================

// Sanity check — diagnose case-mismatch giữa DEVICE_CODE và MQTT_USERNAME.
//
// Backend convention (IotApiKeyService.GenerateMqttCredential):
//   mqtt_username = deviceCode.ToLowerInvariant()
//
// ACL pattern `solar/%u/...` dùng username = lowercase, nhưng backend
// `AdminIotDevicesController.SendCommand` publish topic dùng RAW deviceCode
// (`solar/{device.DeviceCode}/cmd`). Nếu deviceCode KHÔNG lowercase, topic
// backend gửi sẽ KHÔNG match ACL subscription của device → mất downlink.
//
// → Workaround Sprint 4: admin PHẢI tạo device với deviceCode đã lowercase.
//   Sprint 5+ backend nên lowercase topic build hoặc dùng MqttUsername field.
void warnIfCaseMismatch() {
  const char* dev = identity::deviceCode();
  const char* user = MQTT_USERNAME;
  if (!dev || !user) return;

  size_t devLen = strlen(dev);
  size_t userLen = strlen(user);
  if (devLen != userLen) {
    Serial.printf("[mqtt] ⚠ DEVICE_CODE='%s' (len %u) khác MQTT_USERNAME='%s' (len %u) — "
                  "backend MqttUsername convention = lowercase(DeviceCode).\n",
                  dev, static_cast<unsigned>(devLen),
                  user, static_cast<unsigned>(userLen));
    return;
  }
  for (size_t i = 0; i < devLen; ++i) {
    if (static_cast<char>(tolower(static_cast<unsigned char>(dev[i]))) != user[i]) {
      Serial.printf("[mqtt] ⚠ MQTT_USERNAME='%s' KHÔNG phải lowercase(DEVICE_CODE='%s') — "
                    "downlink solar/{deviceCode}/cmd có thể NOT match ACL solar/%%u/cmd.\n",
                    user, dev);
      Serial.println("[mqtt]    → tạo device với deviceCode đã lowercase (vd 'gw-esp32-001'),");
      Serial.println("[mqtt]    → hoặc backend Sprint 5+ phải lowercase khi build topic.");
      return;
    }
  }
  Serial.printf("[mqtt] sanity OK — DEVICE_CODE='%s' lowercase khớp MQTT_USERNAME.\n", dev);
}

bool mqttBegin() {
  if (s_inited) return true;

  if (!loadCaCert()) {
#if MQTT_USE_TLS
    Serial.println("[mqtt] mqttBegin FAIL — CA cert chưa sẵn sàng");
    return false;
#endif
  }

  s_mqtt.setServer(MQTT_BROKER_HOST, MQTT_BROKER_PORT);
  s_mqtt.setBufferSize(MQTT_MAX_PACKET_SIZE);
  s_mqtt.setKeepAlive(MQTT_KEEPALIVE_SEC);
  s_mqtt.setCallback(onMessage);

  warnIfCaseMismatch();

  s_inited = true;
  Serial.printf("[mqtt] init OK — broker=%s:%d tls=%d buf=%u keepalive=%ds\n",
                MQTT_BROKER_HOST, MQTT_BROKER_PORT, MQTT_USE_TLS,
                static_cast<unsigned>(MQTT_MAX_PACKET_SIZE),
                static_cast<int>(MQTT_KEEPALIVE_SEC));
  return true;
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
bool publishWithStats(const char* topic, const char* payload, size_t len,
                     uint8_t qos, bool retain) {
  if (!s_mqtt.connected()) {
    s_pubFail++;
    s_consecutiveFail++;
    return false;
  }
  // PubSubClient::publish overload: (topic, payload, length, retain).
  // Lưu ý: không có overload nhận QoS publish cho v2.8 — QoS 0 mặc định.
  // Để giữ "QoS 1" spec, dùng beginPublish/write/endPublish (tự set QoS).
  // → Sprint 4: dùng publish (QoS 0) cho telemetry/heartbeat (acceptable per spec
  //   "publish/subscribe QoS 0..1"); LWT vẫn được set QoS 1 ở connect().
  //   Production có thể nâng cấp lên MQTTnet hoặc esp-mqtt nếu cần QoS 1 strict.
  (void)qos;
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
  snprintf(topicBuf, kTopicBufLen, "%s/%s/%s/telemetry",
           MQTT_TOPIC_PREFIX, identity::deviceCode(), batterySerial);
  return publishWithStats(topicBuf, payload, payloadLen, 1, false);
}

bool mqttPublishHeartbeat(const char* payload, size_t payloadLen) {
  static char topicBuf[kTopicBufLen];
  snprintf(topicBuf, kTopicBufLen, "%s/%s/heartbeat",
           MQTT_TOPIC_PREFIX, identity::deviceCode());
  return publishWithStats(topicBuf, payload, payloadLen, 1, false);
}

bool mqttPublishStatus(const char* payload, size_t payloadLen) {
  static char topicBuf[kTopicBufLen];
  snprintf(topicBuf, kTopicBufLen, "%s/%s/status",
           MQTT_TOPIC_PREFIX, identity::deviceCode());
  return publishWithStats(topicBuf, payload, payloadLen, 1, true);
}

bool mqttPublishCmdAck(const char* payload, size_t payloadLen) {
  static char topicBuf[kTopicBufLen];
  snprintf(topicBuf, kTopicBufLen, "%s/%s/cmd/ack",
           MQTT_TOPIC_PREFIX, identity::deviceCode());
  return publishWithStats(topicBuf, payload, payloadLen, 1, false);
}

// ---- Stats ----
uint32_t mqttPublishOkCount()         { return s_pubOk; }
uint32_t mqttPublishFailCount()       { return s_pubFail; }
uint32_t mqttConnectCount()           { return s_connectCount; }
uint32_t mqttConsecutiveFailCount()   { return s_consecutiveFail; }
void     mqttResetConsecutiveFails()  { s_consecutiveFail = 0; }

}  // namespace net
